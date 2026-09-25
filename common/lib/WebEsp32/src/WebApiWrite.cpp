// Body-size guards and state-changing routes of WebServices (stage 05, D9,
// D16, D23). Every handler runs on the AsyncTCP task: it checks the gate at
// the route table's access level first (Write = session + CSRF), then only
// parses its input against the immutable schema/HwProjectConfig tables and
// hands the change to the loop task through the command queue
// (postCommand), the SessionGate, or the ExportSlot/CommandResultBoard
// under _mux. No CommonState/ConfigEngine access here; no secret is echoed.
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <esp_heap_caps.h>
#include <BackupCodec.h>
#include <BackupPrecheck.h>
#include <Command.h>
#include <CookieParse.h>
#include <JsonOut.h>
#include <RequestGate.h>
#include <WebInput.h>
#include <WebRoutes.h>
#include "WebHttp.h"
#include "WebServices.h"

namespace {

using webhttp::admit;
using webhttp::formParam;
using webhttp::JSON_TYPE;
using webhttp::sendJson;

constexpr const char* GUARD_ATTR = "webGuard";   // why an attach-time guard refused
constexpr long GUARD_EMPTY = 100;
constexpr long GUARD_TOO_LARGE = 101;
constexpr const char* IMPORT_NOMEM_ATTR = "importNoMem";
constexpr size_t COOKIE_TEXT_MAX = 128;
// Heap headroom over the export length for AsyncBasicResponse's String copy.
constexpr size_t EXPORT_COPY_SLACK = 64;
constexpr size_t GOT_TYPE_MAX = 32;
constexpr const char* RESET_CONFIRM = "RESET";

const char* const TOO_LARGE_JSON = "{\"ok\":false,\"error\":\"too_large\"}";

bool isPost(AsyncWebServerRequest* r) {
    return r->method() == AsyncWebRequestMethod::HTTP_POST;
}

bool isImportUrl(AsyncWebServerRequest* r) {
    return r->url().equals(routeSpec(RouteId::BackupImport).path);
}

// GuardFormBody filter (D23, review-5 Must-fix): any method, any path, with
// a declared body over WEB_FORM_BODY_MAX, except the POST backup import (own
// cap, GuardImportBody) and /ota/* (OtaAny). Chunked bodies without a length
// are never form-parsed by the library (_parseChunkedBytes -> handleBody).
bool formBodyTooLarge(AsyncWebServerRequest* r) {
    return formBodyGuardRefuses(isPost(r), r->url().c_str(), r->contentLength());
}

void sendError(AsyncWebServerRequest* r, int code, const char* key) {
    char body[80];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", key);
    sendJson(r, code, body);
}

void sendInputError(AsyncWebServerRequest* r, InputError e) {
    sendError(r, 400, inputErrorKey(e));
}

// Import body buffer: malloc(total + 1) in _tempObject; buf[total] is a
// sentinel (1) until the last chunk has been copied, then the NUL.
bool importBodyComplete(AsyncWebServerRequest* r, size_t total) {
    const char* buf = static_cast<const char*>(r->_tempObject);
    return buf != nullptr && total > 0 && buf[total] == '\0';
}

}  // namespace

// Attach-time body guards (route registration order step 1), registered
// before every other handler (WebServerHost's OTA guards, the stage-04/05
// routes, serveStatic and the final catch-all). BodyGuardHandler
// (RequestGate.h) drops a refused body unread.
void WebServices::installEarly(AsyncWebServer& s) {
    AsyncWebHandler& form = s.addHandler(new BodyGuardHandler(
        RouteId::GuardFormBody, [](AsyncWebServerRequest* r) { sendJson(r, 413, TOO_LARGE_JSON); }));
    form.setFilter([](AsyncWebServerRequest* r) { return formBodyTooLarge(r); });

    AsyncWebHandler& imp = s.addHandler(new BodyGuardHandler(RouteId::GuardImportBody, [](AsyncWebServerRequest* r) {
        long why = r->getAttribute(GUARD_ATTR, 0L);
        if (why == GUARD_EMPTY) {
            sendError(r, 400, "empty");
        } else if (why == GUARD_TOO_LARGE) {
            sendJson(r, 413, TOO_LARGE_JSON);
        } else if (why > 0 && why <= static_cast<long>(GateResult::NoOtaGrant)) {
            sendGateDenied(r, static_cast<GateResult>(why));
        } else {
            sendError(r, 400, "invalid");   // unreachable: the filter always sets a reason
        }
    }));
    imp.setFilter([this](AsyncWebServerRequest* r) {
        if (!isPost(r) || !isImportUrl(r)) {
            return false;
        }
        GateResult res = _gate.check(r, routeSpec(RouteId::GuardImportBody).access);
        long why = 0;
        if (res != GateResult::Ok) {
            why = static_cast<long>(res);
        } else if (r->contentLength() == 0) {
            why = GUARD_EMPTY;   // also a chunked upload (no declared length)
        } else if (r->contentLength() > BACKUP_MAX_BYTES) {
            why = GUARD_TOO_LARGE;
        } else {
            return false;   // admitted: the BackupImport route buffers it
        }
        r->setAttribute(GUARD_ATTR, why);
        return true;
    });
}

void WebServices::postAndAnswer(AsyncWebServerRequest* r, const Command& c) {
    if (c.type == CommandType::None) {
        sendError(r, 400, "too_long");   // a builder refused the input (text overflow / null address)
        return;
    }
    if (!postCommand(c)) {
        sendError(r, 503, "busy");   // queue full; the stale board expectation ages out
        return;
    }
    char body[48];
    snprintf(body, sizeof(body), "{\"ok\":true,\"id\":%lu}", static_cast<unsigned long>(c.id));
    sendJson(r, 202, body);
}

void WebServices::installWrite(AsyncWebServer& s) {
    // POST /api/logout (Write): revoke the session, clear the cookie.
    s.on(routeMatcher(RouteId::Logout), routeMethod(RouteId::Logout), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::Logout)) {
            return;
        }
        _gate.logout(r);
        char cookie[COOKIE_TEXT_MAX];
        AsyncWebServerResponse* resp = r->beginResponse(200, JSON_TYPE, "{\"ok\":true}");
        resp->addHeader("Cache-Control", "no-store");
        if (formatSessionCookie("", 0, cookie, sizeof(cookie)) > 0) {
            resp->addHeader("Set-Cookie", cookie);
        }
        r->send(resp);
    });

    // POST /api/config (Write): one setting per request, form key/value (D16).
    s.on(routeMatcher(RouteId::ConfigSet), routeMethod(RouteId::ConfigSet), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::ConfigSet)) {
            return;
        }
        const String* key = formParam(r, "key");
        const String* value = formParam(r, "value");
        ParsedSetting ps{};
        InputError e = parseSettingInput(_schema, key != nullptr ? key->c_str() : nullptr,
            value != nullptr ? value->c_str() : nullptr, value != nullptr ? value->length() : 0, ps);
        if (e != InputError::Ok) {
            sendInputError(r, e);   // wifiSsid/wifiPass -> wifi_key: Wi-Fi only via /api/wifi
            return;
        }
        uint32_t id = nextCmdId();
        Command c = ps.type == SettingType::Text ? makeSetText(ps.index, ps.text, EventReason::Web, id)
                                                 : makeSetNumber(ps.index, ps.number, EventReason::Web, id);
        postAndAnswer(r, c);
    });

    // POST /api/sensors/assign (Write): form logical, addr.
    s.on(routeMatcher(RouteId::SensorAssign), routeMethod(RouteId::SensorAssign), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::SensorAssign)) {
            return;
        }
        const String* logical = formParam(r, "logical");
        const String* addr = formParam(r, "addr");
        uint8_t index = 0;
        uint8_t rom[8] = {};
        InputError e = parseLogicalIndex(logical != nullptr ? logical->c_str() : nullptr, _hwCfg.sensorCount, index);
        if (e == InputError::Ok) {
            e = parseRomAddress(addr != nullptr ? addr->c_str() : nullptr, addr != nullptr ? addr->length() : 0, rom);
        }
        if (e != InputError::Ok) {
            sendInputError(r, e);
            return;
        }
        postAndAnswer(r, makeAssignSensor(index, rom, EventReason::Web, nextCmdId()));
    });

    // POST /api/sensors/clear (Write): form logical.
    s.on(routeMatcher(RouteId::SensorClear), routeMethod(RouteId::SensorClear), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::SensorClear)) {
            return;
        }
        const String* logical = formParam(r, "logical");
        uint8_t index = 0;
        InputError e = parseLogicalIndex(logical != nullptr ? logical->c_str() : nullptr, _hwCfg.sensorCount, index);
        if (e != InputError::Ok) {
            sendInputError(r, e);
            return;
        }
        postAndAnswer(r, makeClearSensor(index, EventReason::Web, nextCmdId()));
    });

    // POST /api/sensors/rescan (Write).
    s.on(routeMatcher(RouteId::SensorRescan), routeMethod(RouteId::SensorRescan), [this](AsyncWebServerRequest* r) {
        if (admit(_gate, r, RouteId::SensorRescan)) {
            postAndAnswer(r, makeRescanOneWire(EventReason::Web, nextCmdId()));
        }
    });

    // POST /api/factory-reset (Write): form confirm=RESET, then the queue
    // (CoreRuntime runs the reset and the reboot on the loop task).
    s.on(routeMatcher(RouteId::FactoryReset), routeMethod(RouteId::FactoryReset), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::FactoryReset)) {
            return;
        }
        const String* confirm = formParam(r, "confirm");
        if (confirm == nullptr || !confirm->equals(RESET_CONFIRM)) {
            sendError(r, 400, "confirm");
            return;
        }
        postAndAnswer(r, makeFactoryReset(EventReason::Web, nextCmdId()));
    });

    // POST /api/ota/grant (Write): password re-entry, shared login throttle (D5/D6).
    s.on(routeMatcher(RouteId::OtaGrant), routeMethod(RouteId::OtaGrant), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::OtaGrant)) {
            return;
        }
        const String* pass = formParam(r, "pass");
        if (pass == nullptr || pass->length() == 0) {
            sendError(r, 400, "invalid");
            return;
        }
        uint32_t retryS = 0;
        SessionGate::LoginResult res = _gate.grantOta(r, pass->c_str(), pass->length(), retryS);
        if (res == SessionGate::LoginResult::Throttled) {
            char body[64];
            snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"throttled\",\"retryS\":%lu}",
                static_cast<unsigned long>(retryS));
            sendJson(r, 429, body);
            return;
        }
        if (res != SessionGate::LoginResult::Ok) {
            sendError(r, 401, "credentials");
            return;
        }
        char body[48];
        snprintf(body, sizeof(body), "{\"ok\":true,\"ttlS\":%lu}",
            static_cast<unsigned long>(OTA_GRANT_TTL_MS / 1000));
        sendJson(r, 200, body);
    });

    // POST /api/backup/export (Write): ask the loop task to build the file (D9).
    s.on(routeMatcher(RouteId::BackupExportReq), routeMethod(RouteId::BackupExportReq),
        [this](AsyncWebServerRequest* r) {
            if (!admit(_gate, r, RouteId::BackupExportReq)) {
                return;
            }
            portENTER_CRITICAL(&_mux);
            _export.request();
            portEXIT_CRITICAL(&_mux);
            sendJson(r, 202, "{\"ok\":true}");
        });

    // GET /api/backup/export (Read): 202 while pending, the file once ready
    // (claimed exactly once), 404 when nothing was requested.
    s.on(routeMatcher(RouteId::BackupExportGet), routeMethod(RouteId::BackupExportGet),
        [this](AsyncWebServerRequest* r) {
            if (!admit(_gate, r, RouteId::BackupExportGet)) {
                return;
            }
            char* buf = nullptr;
            size_t len = 0;
            portENTER_CRITICAL(&_mux);
            ExportState st = _export.state();
            bool taken = _export.take(buf, len);
            portEXIT_CRITICAL(&_mux);
            if (!taken) {
                if (st == ExportState::Requested) {
                    sendJson(r, 202, "{\"ok\":true,\"pending\":true}");
                } else {
                    sendJson(r, 404, "{\"ok\":false,\"error\":\"none\"}");
                }
                return;
            }
            // AsyncBasicResponse copies the NUL-terminated text into a String;
            // a failed copy is silent (empty 200 attachment) and 3.12.1 has no
            // content-length getter, so the copy's heap is checked up front.
            // On failure the buffer goes back to the slot (the GET can be
            // retried) and the answer is 503, never an empty download
            // (review-5 Should-fix).
            AsyncWebServerResponse* resp = nullptr;
            if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > len + EXPORT_COPY_SLACK) {
                resp = r->beginResponse(200, JSON_TYPE, buf);
            }
            if (resp == nullptr) {
                portENTER_CRITICAL(&_mux);
                // Idle: nothing newer was requested meanwhile. Otherwise a new
                // build is on its way and this copy is dropped (never freed
                // under the lock).
                if (_export.state() == ExportState::Idle) {
                    _export.publish(buf, len, nowMs());
                    buf = nullptr;
                }
                portEXIT_CRITICAL(&_mux);
                free(buf);
                sendError(r, 503, "no_memory");
                return;
            }
            free(buf);   // the response holds its own copy
            char disposition[80];
            snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s-backup.json\"",
                _schema.controllerType);
            resp->addHeader("Content-Disposition", disposition);
            resp->addHeader("Cache-Control", "no-store");
            r->send(resp);
        });

    installBackupImport(s);
}

// POST /api/backup/import (Write): raw application/json body <= BACKUP_MAX_BYTES.
// GuardImportBody has already refused (at attach time, before any body byte)
// a request without Write access, without a declared length or over the cap,
// so onBody only ever sees an admitted body of at most BACKUP_MAX_BYTES.
void WebServices::installBackupImport(AsyncWebServer& s) {
    auto onBody = [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0 && r->_tempObject == nullptr && total > 0 && total <= BACKUP_MAX_BYTES) {
            char* buf = static_cast<char*>(malloc(total + 1));
            if (buf == nullptr) {
                r->setAttribute(IMPORT_NOMEM_ATTR, true);
                return;
            }
            buf[total] = 1;   // sentinel: not complete yet
            r->_tempObject = buf;
        }
        char* buf = static_cast<char*>(r->_tempObject);
        if (buf == nullptr || total > BACKUP_MAX_BYTES || index > total || len > total - index) {
            return;   // no buffer or out of bounds: stays incomplete -> 400
        }
        memcpy(buf + index, data, len);
        if (index + len == total) {
            buf[total] = '\0';
        }
    };
    s.on(routeMatcher(RouteId::BackupImport), routeMethod(RouteId::BackupImport),
        [this](AsyncWebServerRequest* r) {
            if (!admit(_gate, r, RouteId::BackupImport)) {
                return;
            }
            size_t len = r->contentLength();
            if (!importBodyComplete(r, len)) {
                if (r->getAttribute(IMPORT_NOMEM_ATTR, false)) {
                    sendError(r, 503, "no_memory");
                } else {
                    sendError(r, 400, "empty");   // no raw body (e.g. a form-encoded post) or incomplete
                }
                return;
            }
            char* buf = static_cast<char*>(r->_tempObject);
            char got[GOT_TYPE_MAX];
            got[0] = '\0';
            PrecheckResult pr = precheckBackup(buf, len, _schema.controllerType, got, sizeof(got));
            if (pr != PrecheckResult::Ok) {
                char body[256];   // got: <= 31 chars, JSON-escaped
                JsonOut j(body, sizeof(body));
                j.beginObject();
                j.key("ok");
                j.boolean(false);
                j.key("error");
                j.str(precheckKey(pr));
                j.key("got");
                j.str(got);
                j.endObject();
                int code = pr == PrecheckResult::TooLarge ? 413 : 400;
                if (!j.ok()) {
                    sendError(r, code, precheckKey(pr));
                } else {
                    sendJson(r, code, body);
                }
                return;
            }
            Command c = makeImportBackup(buf, len, EventReason::Web, nextCmdId());
            if (!postCommand(c)) {
                sendError(r, 503, "busy");   // _tempObject kept: the request destructor frees it
                return;
            }
            r->_tempObject = nullptr;   // the queue owns the payload now (CoreRuntime frees it)
            char body[48];
            snprintf(body, sizeof(body), "{\"ok\":true,\"id\":%lu}", static_cast<unsigned long>(c.id));
            sendJson(r, 202, body);
        },
        nullptr, onBody);
}
