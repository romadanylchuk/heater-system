#include "WebServerHost.h"
#include <CommonState.h>
#include <ElegantOTA.h>
#include <OtaGuard.h>
#include <WifiCredsMailbox.h>
#include <string.h>
#include "SetupPage.h"

namespace {

constexpr const char* JSON_TYPE = "application/json";

// Every URL ElegantOTA can serve: it registers "/update" as a plain string,
// i.e. BackwardCompatible matching ("/update" and "/update/..."), plus
// "/ota/..." (review-9 Should-fix 1).
bool isOtaUrl(const String& url) {
    return url == "/update" || url.startsWith("/update/") || url.startsWith("/ota/");
}

// Form (body) parameter, or nullptr when absent.
const String* formParam(AsyncWebServerRequest* r, const char* name) {
    const AsyncWebParameter* p = r->getParam(name, true);
    return p != nullptr ? &p->value() : nullptr;
}

}  // namespace

bool WebServerHost::guard(AsyncWebServerRequest* r) const {
    if (_auth->check(r)) {
        return true;
    }
    _auth->challenge(r);
    return false;
}

void WebServerHost::sendSnapshot(AsyncWebServerRequest* r, const JsonSnapshot& snap) const {
    // 1.5 KB on the AsyncTCP task stack (16 KB): the snapshot is copied out
    // under its mutex and the response keeps its own copy.
    char buf[JSON_SNAPSHOT_CAP];
    if (snap.copy(buf, sizeof(buf)) == 0) {
        r->send(503, JSON_TYPE, "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    r->send(200, JSON_TYPE, buf);
}

void WebServerHost::handleWifiSave(AsyncWebServerRequest* r) {
    const String* ssid = formParam(r, "ssid");
    const String* pass = formParam(r, "pass");
    const char* ssidStr = ssid != nullptr ? ssid->c_str() : nullptr;
    const char* passStr = pass != nullptr ? pass->c_str() : nullptr;
    size_t ssidLen = ssid != nullptr ? ssid->length() : 0;
    size_t passLen = pass != nullptr ? pass->length() : 0;
    // ssid 1..32; pass empty or a valid WPA2 key (8..63, or 64 hex): a value
    // that can never join would lock a Station-mode device out (Should-fix 4).
    if (checkWifiCreds(ssidStr, ssidLen, passStr, passLen) != WifiCredsCheck::Ok) {
        r->send(400, JSON_TYPE, "{\"ok\":false,\"error\":\"invalid\"}");
        return;
    }
    // One atomic hand-over of the whole pair; ConnectivityServices::tick()
    // applies both settings in the same loop-task call, so the supervisor can
    // never see a half-applied change (review-9 Must-fix 2).
    if (!_wifiCreds->offer(ssidStr, ssidLen, passStr, passLen)) {
        r->send(503, JSON_TYPE, "{\"ok\":false,\"error\":\"busy\"}");  // a previous pair is still pending
        return;
    }
    r->send(200, JSON_TYPE, "{\"ok\":true}");
}

void WebServerHost::begin(AdminAuth& auth, JsonSnapshot& status, JsonSnapshot& scan, JsonSnapshot& version,
    NetSignals& signals, WifiCredsMailbox& wifiCreds) {
    if (_begun) {
        return;
    }
    _begun = true;
    _auth = &auth;
    _status = &status;
    _scan = &scan;
    _version = &version;
    _signals = &signals;
    _wifiCreds = &wifiCreds;

    // D17 layer 1 (review-9 Must-fix 1): ElegantOTA is mounted without its own
    // String credentials (they would race with loop-task updates), and its
    // /ota/upload chunk callback writes Update BEFORE any middleware runs --
    // ESPAsyncWebServer 3.12.1 attaches the handler right after the headers
    // (_parseLine -> _attachHandler, which evaluates filter() then
    // canHandle() in registration order), streams the body into that
    // handler's upload/body callbacks, and runs the middleware chain only once
    // the body is consumed. So these guard handlers are registered FIRST and
    // win at attach time for every request that must not reach ElegantOTA:
    // unauthenticated ones, and /ota/* while an espota session owns Update
    // (Must-fix 1c). They have no upload/body callback, so the chunks of such
    // a request are parsed and dropped. The filters test the URL first so
    // unrelated requests (stage-05 routes) never pay for the auth check.
    auto otaRefused = [this](AsyncWebServerRequest* r) {
        return r->url().startsWith("/ota/") && (!_auth->check(r) || _signals->espotaActive.load());
    };
    auto updateRefused = [this](AsyncWebServerRequest* r) {
        const String& url = r->url();
        return (url == "/update" || url.startsWith("/update/")) && !_auth->check(r);
    };
    auto refuse = [this](AsyncWebServerRequest* r) {
        if (!_auth->check(r)) {
            _auth->challenge(r);
        } else {
            r->send(409, "text/plain", "espota update in progress");
        }
    };
    _server.on(AsyncURIMatcher::prefix("/ota/"), HTTP_ANY, refuse).setFilter(otaRefused);
    _server.on(AsyncURIMatcher::exact("/update"), HTTP_ANY, refuse).setFilter(updateRefused);
    _server.on(AsyncURIMatcher::prefix("/update/"), HTTP_ANY, refuse).setFilter(updateRefused);

    // D17 layer 2: the server-level middleware still challenges every OTA URL
    // before any handler's request callback runs.
    _server.addMiddleware([this](AsyncWebServerRequest* r, ArMiddlewareNext next) {
        if (isOtaUrl(r->url()) && !guard(r)) {
            return;
        }
        next();
    });

    ElegantOTA.begin(&_server);  // no credentials: guard handlers + middleware above
    ElegantOTA.setAutoReboot(false);  // the firmware reboots OTA_REBOOT_DELAY_MS after success (D14)
    ElegantOTA.onStart([this]() {
        _signals->otaWebBytes.store(0);  // before the start is visible to the loop task
        _signals->otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
    });
    ElegantOTA.onProgress([this](size_t current, size_t) {
        _signals->otaWebBytes.store(static_cast<uint32_t>(current));
        _signals->otaProgress.fetch_add(1);
    });
    // onEnd(true) only means !Update.hasError(): an upload that wrote nothing
    // is a failure, never OtaUpdate(1) + reboot (final-check S4).
    ElegantOTA.onEnd([this](bool ok) { _signals->otaEnded.store(webOtaEndCode(ok, _signals->otaWebBytes.load())); });

    _server.on(AsyncURIMatcher::exact("/api/wifi/status"), HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            sendSnapshot(r, *_status);
        }
    });
    _server.on(AsyncURIMatcher::exact("/api/wifi/scan"), HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            sendSnapshot(r, *_scan);
        }
    });
    _server.on(AsyncURIMatcher::exact("/api/wifi/scan"), HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            _signals->scanRequested.store(true);
            r->send(202, JSON_TYPE, "{\"ok\":true}");
        }
    });
    _server.on(AsyncURIMatcher::exact("/api/wifi"), HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            handleWifiSave(r);
        }
    });
    _server.on(AsyncURIMatcher::exact("/api/version"), HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            sendSnapshot(r, *_version);
        }
    });
    _server.on(AsyncURIMatcher::exact("/setup"), HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            r->send(200, "text/html", reinterpret_cast<const uint8_t*>(SETUP_HTML), strlen(SETUP_HTML));
        }
    });
    _server.on(AsyncURIMatcher::exact("/"), HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (guard(r)) {
            if (_setupApActive.load()) {
                r->redirect("/setup");
            } else {
                r->send(404);  // stage 05 serves the SPA here
            }
        }
    });

    _server.begin();
}
