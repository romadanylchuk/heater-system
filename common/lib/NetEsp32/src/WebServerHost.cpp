#include "WebServerHost.h"
#include <CommonState.h>
#include <ElegantOTA.h>
#include <OtaGuard.h>
#include <WifiCredsMailbox.h>
#include <string.h>
#include "RescuePage.h"

namespace {

constexpr const char* JSON_TYPE = "application/json";
// Request attribute set by the /ota/* guard filter on admission (attach time).
constexpr const char* OTA_ADMITTED_ATTR = "otaAdmitted";

// True when the request URL starts with the route's (prefix) path.
bool urlStartsWith(AsyncWebServerRequest* r, RouteId id) {
    return r->url().startsWith(routeSpec(id).path);
}

// ElegantOTA's "GET /ota/start?mode=fs|..." (its own route string, under
// the OtaAny prefix): the only request that selects firmware vs filesystem.
constexpr const char* OTA_START_SUFFIX = "start";

// Mirrors the library's matcher for ElegantOTA's string route (3.12.1
// AsyncURIMatcher BackwardCompatible: url == route || url.startsWith(route
// + "/")), so "/ota/start/<x>" - which also runs ElegantOTA's start
// handler - sets the fs flag too (review-5 iteration 1 suggestion).
bool isOtaStart(AsyncWebServerRequest* r) {
    const char* prefix = routeSpec(RouteId::OtaAny).path;
    size_t plen = strlen(prefix);
    size_t slen = strlen(OTA_START_SUFFIX);
    const String& url = r->url();
    if (url.length() < plen + slen || !url.startsWith(prefix) ||
        strncmp(url.c_str() + plen, OTA_START_SUFFIX, slen) != 0) {
        return false;
    }
    char next = url.c_str()[plen + slen];
    return next == '\0' || next == '/';
}

// Form (body) parameter, or nullptr when absent.
const String* formParam(AsyncWebServerRequest* r, const char* name) {
    const AsyncWebParameter* p = r->getParam(name, true);
    return p != nullptr ? &p->value() : nullptr;
}

}  // namespace

void sendRescuePage(AsyncWebServerRequest* r) {
    r->send(200, "text/html", reinterpret_cast<const uint8_t*>(RESCUE_HTML), sizeof(RESCUE_HTML) - 1);
}

GateResult WebServerHost::gateCheck(AsyncWebServerRequest* r, Access level) const {
    if (level == Access::Public) {
        return GateResult::Ok;
    }
    if (_gate == nullptr) {
        return GateResult::NoSession;  // no gate attached: fail closed
    }
    return _gate->check(r, level);
}

bool WebServerHost::guard(AsyncWebServerRequest* r, Access level) const {
    GateResult res = gateCheck(r, level);
    if (res == GateResult::Ok) {
        return true;
    }
    sendGateDenied(r, res);
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
    NetSignals& signals, WifiCredsMailbox& wifiCreds, const RequestGate* gate, WebRouteInstaller installer,
    void* installerCtx) {
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
    _gate = gate;

    // 1. Stage-05 early guards (form/import body size), before anything else
    //    can attach to their requests.
    if (installer != nullptr) {
        installer(_server, WebInstallPhase::Early, installerCtx);
    }

    // 2. OTA guards (D17 layer 1, stage 05 D5). ElegantOTA is mounted without
    //    its own String credentials, and its /ota/upload chunk callback writes
    //    Update BEFORE any middleware runs (see the header). These handlers are
    //    registered before ElegantOTA and win at attach time for every /ota/*
    //    request that is not session + CSRF + OTA-grant authorised, and for
    //    every /ota/* request while an espota session owns Update (409). The
    //    /ota/* refusal is a BodyGuardHandler (RequestGate.h): a trivial
    //    handler, so the library only counts and drops the refused body (no
    //    urlencoded/multipart parsing, no upload callback, nothing reaches
    //    Update); the answer is sent once the body has been received. The
    //    stage-05 global form-body guard (Early, above) exempts /ota/*, so
    //    this handler owns every refused /ota/* body. The filter tests the
    //    URL first so unrelated requests never pay for the gate check.
    //    A request this filter admits is marked (OTA_ADMITTED_ATTR, a
    //    server-side attribute no client can set) so the layer-2 middleware
    //    does not re-check it: ElegantOTA's upload callback has already run
    //    Update.end(true) by the time the middleware runs, and refusing then
    //    (grant expired / session evicted mid-upload) would skip onEnd and
    //    leave a committed image with relays inhibited (review-3 Should-fix 1).
    auto otaRefused = [this](AsyncWebServerRequest* r) {
        if (!urlStartsWith(r, RouteId::OtaAny)) {
            return false;
        }
        bool refused = gateCheck(r, routeSpec(RouteId::OtaAny).access) != GateResult::Ok ||
                       _signals->espotaActive.load();
        if (!refused) {
            r->setAttribute(OTA_ADMITTED_ATTR, true);
        }
        return refused;
    };
    auto otaRefuse = [this](AsyncWebServerRequest* r) {
        GateResult res = gateCheck(r, routeSpec(RouteId::OtaAny).access);
        if (res != GateResult::Ok) {
            sendGateDenied(r, res);
        } else {
            r->send(409, "text/plain", "espota update in progress");
        }
    };
    _server.addHandler(new BodyGuardHandler(RouteId::OtaAny, otaRefuse)).setFilter(otaRefused);
    // ElegantOTA registers "/update" as a plain string (BackwardCompatible:
    // "/update" and "/update/..."); both are fully owned here, so its page
    // (whose JS cannot send the CSRF header) is unreachable. No body callback.
    _server.on(routeMatcher(RouteId::UpdatePage), routeMethod(RouteId::UpdatePage), [](AsyncWebServerRequest* r) {
        if (r->method() == AsyncWebRequestMethod::HTTP_GET) {
            sendRescuePage(r);
        } else {
            r->send(405);
        }
    });
    _server.on(routeMatcher(RouteId::UpdateSub), routeMethod(RouteId::UpdateSub),
        [](AsyncWebServerRequest* r) { r->send(404); });

    // 3. D17 layer 2: the server-level middleware re-checks every /ota/* URL
    //    before any handler's request callback runs, except requests the
    //    attach-time guard filter already admitted (see above): any /ota/*
    //    request that attached without passing that filter is still refused.
    _server.addMiddleware([this](AsyncWebServerRequest* r, ArMiddlewareNext next) {
        if (urlStartsWith(r, RouteId::OtaAny) && !r->hasAttribute(OTA_ADMITTED_ATTR) &&
            !guard(r, routeSpec(RouteId::OtaAny).access)) {
            return;
        }
        if (r->hasAttribute(OTA_ADMITTED_ATTR) && isOtaStart(r)) {
            // Mode of the start about to run; onStart (called from this
            // request's handler) latches it (review-5 suggestion).
            const AsyncWebParameter* mode = r->getParam("mode");
            _signals->otaWebFsPending.store(mode != nullptr && mode->value() == "fs");
        }
        next();
    });

    // 4. ElegantOTA, stage-04 callbacks unchanged.
    ElegantOTA.begin(&_server);  // no credentials: guard handlers + middleware above
    ElegantOTA.setAutoReboot(false);  // the firmware reboots OTA_REBOOT_DELAY_MS after success (D14)
    ElegantOTA.onStart([this]() {
        _signals->otaWebBytes.store(0);  // before the start is visible to the loop task
        _signals->otaStarted.store(static_cast<uint8_t>(OtaSource::Web));
        if (_signals->otaWebFsPending.load()) {
            _signals->otaWebFsStarted.store(true);  // before the start count: readers see both
        }
        _signals->webOtaStarts.fetch_add(1);  // static LittleFS serving stops now, not a tick later
    });
    ElegantOTA.onProgress([this](size_t current, size_t) {
        _signals->otaWebBytes.store(static_cast<uint32_t>(current));
        _signals->otaProgress.fetch_add(1);
    });
    // onEnd(true) only means !Update.hasError(): an upload that wrote nothing
    // is a failure, never OtaUpdate(1) + reboot (final-check S4).
    ElegantOTA.onEnd([this](bool ok) { _signals->otaEnded.store(webOtaEndCode(ok, _signals->otaWebBytes.load())); });

    // 5. Stage-04 routes, gated at the route table's access level.
    _server.on(routeMatcher(RouteId::WifiStatus), routeMethod(RouteId::WifiStatus), [this](AsyncWebServerRequest* r) {
        if (guard(r, routeSpec(RouteId::WifiStatus).access)) {
            sendSnapshot(r, *_status);
        }
    });
    _server.on(routeMatcher(RouteId::WifiScanGet), routeMethod(RouteId::WifiScanGet), [this](AsyncWebServerRequest* r) {
        if (guard(r, routeSpec(RouteId::WifiScanGet).access)) {
            sendSnapshot(r, *_scan);
        }
    });
    _server.on(routeMatcher(RouteId::WifiScanPost), routeMethod(RouteId::WifiScanPost), [this](AsyncWebServerRequest* r) {
        if (guard(r, routeSpec(RouteId::WifiScanPost).access)) {
            _signals->scanRequested.store(true);
            r->send(202, JSON_TYPE, "{\"ok\":true}");
        }
    });
    _server.on(routeMatcher(RouteId::WifiSave), routeMethod(RouteId::WifiSave), [this](AsyncWebServerRequest* r) {
        if (guard(r, routeSpec(RouteId::WifiSave).access)) {
            handleWifiSave(r);
        }
    });
    _server.on(routeMatcher(RouteId::Version), routeMethod(RouteId::Version), [this](AsyncWebServerRequest* r) {
        if (guard(r, routeSpec(RouteId::Version).access)) {  // Public (D10)
            sendSnapshot(r, *_version);
        }
    });
    _server.on(routeMatcher(RouteId::SetupPage), routeMethod(RouteId::SetupPage), [this](AsyncWebServerRequest* r) {
        if (guard(r, routeSpec(RouteId::SetupPage).access)) {  // Public: the page logs in itself
            sendRescuePage(r);
        }
    });
    // The exact GET "/" handler is gone: stage 05's Late installer owns "/".

    // 6. Stage-05 routes (API, "/", static files, final catch-all).
    if (installer != nullptr) {
        installer(_server, WebInstallPhase::Late, installerCtx);
    }

    // 7.
    _server.begin();
}
