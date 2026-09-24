#pragma once
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <NetSignals.h>
#include <WifiCredsMailbox.h>
#include "AdminAuth.h"
#include "JsonSnapshot.h"

// The stage-04-owned AsyncWebServer on port 80 (D17/D18). Stage 05 adds its
// routes/middleware on the same instance via server() and reuses adminAuth.
//
// Threading: every handler/callback here runs on the AsyncTCP task and only
// touches the AdminAuth lock-guarded copy, the JsonSnapshot mutexes, the
// NetSignals atomics, _setupApActive and the lock-free WifiCredsMailbox --
// never CommonState/ConfigEngine.
//
// Routes (each guarded by AdminAuth::check, 401 digest challenge otherwise):
//   GET  /api/wifi/status   status snapshot
//   GET  /api/wifi/scan     cached scan snapshot
//   POST /api/wifi/scan     request a scan -> 202
//   POST /api/wifi          form ssid (1..32), pass ("" or 8..63, or 64 hex) -> WifiCredsMailbox
//                           (both applied together on the loop task); 400 invalid, 503 pending
//   GET  /api/version       version snapshot
//   GET  /setup             PROGMEM setup page
//   GET  /                  302 -> /setup only while the setup AP is active, else 404
// plus ElegantOTA's /update, /update/... and /ota/* (no ElegantOTA auth).
// Those are guarded twice: by guard handlers registered before ElegantOTA,
// whose filter runs when the handler is attached (after the headers, BEFORE
// the body -- so an unauthenticated upload never reaches ElegantOTA's
// Update.write chunk callback), answering 401, or 409 for /ota/* while an
// espota session is active; and by the server-level AdminAuth middleware.
class WebServerHost {
public:
    static constexpr uint16_t HTTP_PORT = 80;

    // Mounts ElegantOTA (auto-reboot off; OTA callbacks -> NetSignals),
    // registers the routes and starts the server. Call once, after the Wi-Fi
    // mode has been initialised. All references must outlive this object.
    void begin(AdminAuth& auth, JsonSnapshot& status, JsonSnapshot& scan, JsonSnapshot& version,
        NetSignals& signals, WifiCredsMailbox& wifiCreds);

    AsyncWebServer& server() { return _server; }

    // Loop task: mirrors state.network.setupApActive for the "/" redirect.
    void setSetupApActive(bool active) { _setupApActive.store(active); }

private:
    bool guard(AsyncWebServerRequest* r) const;
    void sendSnapshot(AsyncWebServerRequest* r, const JsonSnapshot& snap) const;
    void handleWifiSave(AsyncWebServerRequest* r);

    AsyncWebServer _server{HTTP_PORT};
    std::atomic<bool> _setupApActive{false};

    AdminAuth* _auth = nullptr;
    JsonSnapshot* _status = nullptr;
    JsonSnapshot* _scan = nullptr;
    JsonSnapshot* _version = nullptr;
    NetSignals* _signals = nullptr;
    WifiCredsMailbox* _wifiCreds = nullptr;
    bool _begun = false;
};
