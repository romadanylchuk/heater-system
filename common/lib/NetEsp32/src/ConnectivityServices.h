#pragma once
#include <ESPAsyncWebServer.h>
#include <stddef.h>
#include <CommonState.h>
#include <ConnectivityRuntime.h>
#include <CoreServices.h>
#include <HardwareServices.h>
#include <HaEntityRegistry.h>
#include <HwConfig.h>
#include <NetIdentity.h>
#include <NetSignals.h>
#include <NvsKvStore.h>
#include <WifiCredsMailbox.h>
#include "AdminAuth.h"
#include "AsyncMqttTransport.h"
#include "EspOtaPlatform.h"
#include "EspWifiPort.h"
#include "EspotaService.h"
#include "JsonSnapshot.h"
#include "WebServerHost.h"

// Wiring facade for stage 04, constructed next to CoreServices/
// HardwareServices in both main.cpp files: owns the NetEsp32 adapters, the
// pure ConnectivityRuntime, the web server host and the espota service. The
// constructor touches no hardware or network.
//
// Loop order (main.cpp): slow tick core.tick(); hw.tick(); net.tick(); then
// every ~100 ms net.fastTick(); hw.fastTick(). All state changes go through
// the command queue; the only other-task access is to NetSignals atomics,
// AdminAuth's locked copy, the JsonSnapshot mutexes and the queue. Nothing
// here blocks the loop task except espota's own upload inside handle().
//
// Stage-05 hook: setWebLayer() (before begin()) injects the RequestGate
// (session + CSRF + OTA grant; null fails closed: every non-Public route
// answers 401) and the two-phase route installer that WebServerHost::begin
// calls at the Early (body guards) and Late (SPA/API routes) points of its
// registration order. adminAuth() is the only credential source (login,
// OTA re-entry, espota); its epoch() invalidates sessions on any change.
class ConnectivityServices {
public:
    ConnectivityServices(CommonState& state, CoreServices& core, HardwareServices& hw, const NetIdentity& id,
        const HwProjectConfig& hwCfg, const HaCustomEntity* custom = nullptr, size_t customCount = 0);

    void begin();     // after core.begin + hw.begin
    void tick();      // ~1 s, after hw.tick()
    void fastTick();  // ~100 ms, BEFORE hw.fastTick()

    // Must be called before begin(); the pointers must outlive this object.
    void setWebLayer(const RequestGate* gate, WebRouteInstaller installer, void* ctx) {
        _gate = gate;
        _installer = installer;
        _installerCtx = ctx;
    }

    AsyncWebServer& server() { return _web.server(); }
    AdminAuth& adminAuth() { return _adminAuth; }
    // Any task: NetSignals::webOtaStarts (monotonic web OTA start count).
    uint32_t webOtaStarts() const { return _signals.webOtaStarts.load(); }
    // Any task: NetSignals::otaWebFsStarted (a filesystem-mode web OTA has
    // started since boot; sticky).
    bool webOtaFsStarted() const { return _signals.otaWebFsStarted.load(); }
    // Any task: NetSignals::espotaActive (true while an espota transfer runs;
    // the loop task is blocked in handle() for its whole duration).
    bool espotaActive() const { return _signals.espotaActive.load(); }
    ConnectivityRuntime& runtime() { return _runtime; }

private:
    static constexpr size_t WEB_VERSION_FILE_MAX = 64;
    static constexpr size_t CLIENT_ID_MAX = 32;

    static void onEspotaStart(void* ctx);
    void readWebVersion();
    void buildClientId();
    void applyAdminCreds();
    void applyPendingWifiCreds();
    void refreshSnapshots();
    const char* webPass() const;

    CommonState& _state;
    CoreServices& _core;
    HardwareServices& _hw;
    const NetIdentity& _id;
    const HwProjectConfig& _hwCfg;
    const HaCustomEntity* _custom;
    size_t _customCount;

    // Declared before _runtime, which binds references to them.
    EspWifiPort _wifi;
    AsyncMqttTransport _mqtt;
    EspOtaPlatform _ota;
    NvsKvStore _otaStore{NVS_NS_OTA};
    NetSignals _signals;
    ConnectivityRuntime _runtime;

    AdminAuth _adminAuth;
    JsonSnapshot _statusJson;
    JsonSnapshot _scanJson;
    JsonSnapshot _versionJson;
    WifiCredsMailbox _wifiCreds;  // POST /api/wifi -> tick(): SSID+password applied together
    WebServerHost _web;
    EspotaService _espota;

    const RequestGate* _gate = nullptr;
    WebRouteInstaller _installer = nullptr;
    void* _installerCtx = nullptr;

    char _webVersion[VERSION_TEXT_LEN + 1] = {};
    char _clientId[CLIENT_ID_MAX + 1] = {};
    char _jsonBuf[JSON_SNAPSHOT_CAP] = {};  // loop-task scratch for snapshot rebuilds
    bool _active = false;  // runtime begun with a valid identity (Wi-Fi initialised)
};
