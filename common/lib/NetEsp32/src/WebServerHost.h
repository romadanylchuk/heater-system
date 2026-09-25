#pragma once
#include <ESPAsyncWebServer.h>
#include <NetSignals.h>
#include <WifiCredsMailbox.h>
#include "AdminAuth.h"
#include "JsonSnapshot.h"
#include "RequestGate.h"

// Stage 05 two-phase route installer (Route registration order). Early runs
// before the OTA guards (body-size guards), Late after the stage-04 routes
// (SPA/API routes, static serving, final body-inert catch-all).
enum class WebInstallPhase : uint8_t { Early, Late };
using WebRouteInstaller = void (*)(AsyncWebServer& server, WebInstallPhase phase, void* ctx);

// 200 text/html RESCUE_HTML. The only translation unit that includes
// RescuePage.h is WebServerHost.cpp, so the PROGMEM page exists once in
// flash; WebEsp32 serves it (GET / fallback) through this function.
void sendRescuePage(AsyncWebServerRequest* r);

// The stage-04-owned AsyncWebServer on port 80 (D17/D18). Every route is
// registered through routeMatcher(RouteId) from the WebEngine route table
// (D19) and gated by the injected RequestGate at the table's access level
// (stage 05, D2): 401 auth / 403 csrf / 403 ota_grant via sendGateDenied. A
// null gate fails closed: every non-Public route answers 401.
//
// Threading: every handler/callback here runs on the AsyncTCP task and only
// touches the RequestGate (portMUX-guarded session store), the JsonSnapshot
// mutexes, the NetSignals atomics and the lock-free WifiCredsMailbox --
// never CommonState/ConfigEngine.
//
// Registration order (begin):
//   1. installer(Early)            stage-05 body guards (413 before buffering)
//   2. OTA guards:
//        ANY  /ota/*   (OtaAny)     filter: gate.check(Ota) != Ok || espota
//                                   active -> sendGateDenied / 409
//        ANY  /update  (UpdatePage) GET -> RESCUE_HTML, else 405
//        ANY  /update/* (UpdateSub) 404
//   3. middleware: /ota/* again requires gate.check(Ota) == Ok, except for
//      requests the step-2 filter admitted (marked at attach time, so an
//      in-flight upload is never refused after Update.end ran)
//   4. ElegantOTA.begin (its /update page is unreachable behind UpdatePage)
//   5. stage-04 routes:
//        GET  /api/wifi/status   Read   status snapshot
//        GET  /api/wifi/scan     Read   cached scan snapshot
//        POST /api/wifi/scan     Write  request a scan -> 202
//        POST /api/wifi          Write  form ssid (1..32), pass ("" or 8..63,
//                                       or 64 hex) -> WifiCredsMailbox (both
//                                       applied together on the loop task);
//                                       400 invalid, 503 pending
//        GET  /api/version       Public version snapshot (D10)
//        GET  /setup             Public RESCUE_HTML
//   6. installer(Late)             stage-05 API, "/", static files, catch-all
//   7. server.begin()
// Why the OTA guards come first: ESPAsyncWebServer attaches the handler right
// after the headers (_parseLine -> _attachHandler, which evaluates filter()
// then canHandle() in registration order), streams the body into that
// handler's upload/body callbacks, and runs the middleware chain only once
// the body is consumed. ElegantOTA's /ota/upload chunk callback writes Update
// directly, so the guard handlers (no upload/body callback) must win at attach
// time for every non-granted /ota/* request, before any body byte reaches
// Update.write. The middleware is the second layer.
class WebServerHost {
public:
    static constexpr uint16_t HTTP_PORT = 80;

    // Mounts ElegantOTA (auto-reboot off; OTA callbacks -> NetSignals),
    // registers the routes in the order above and starts the server. Call
    // once, after the Wi-Fi mode has been initialised. All references and
    // pointers must outlive this object; gate/installer may be null.
    void begin(AdminAuth& auth, JsonSnapshot& status, JsonSnapshot& scan, JsonSnapshot& version,
        NetSignals& signals, WifiCredsMailbox& wifiCreds, const RequestGate* gate, WebRouteInstaller installer,
        void* installerCtx);

    AsyncWebServer& server() { return _server; }

private:
    GateResult gateCheck(AsyncWebServerRequest* r, Access level) const;
    bool guard(AsyncWebServerRequest* r, Access level) const;
    void sendSnapshot(AsyncWebServerRequest* r, const JsonSnapshot& snap) const;
    void handleWifiSave(AsyncWebServerRequest* r);

    AsyncWebServer _server{HTTP_PORT};

    AdminAuth* _auth = nullptr;
    JsonSnapshot* _status = nullptr;
    JsonSnapshot* _scan = nullptr;
    JsonSnapshot* _version = nullptr;
    NetSignals* _signals = nullptr;
    WifiCredsMailbox* _wifiCreds = nullptr;
    const RequestGate* _gate = nullptr;
    bool _begun = false;
};
