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
// Stage-05 extension points: server() (add routes/middleware on the same
// AsyncWebServer) and adminAuth() (the same admin credential check).
class ConnectivityServices {
public:
    ConnectivityServices(CommonState& state, CoreServices& core, HardwareServices& hw, const NetIdentity& id,
        const HwProjectConfig& hwCfg, const HaCustomEntity* custom = nullptr, size_t customCount = 0);

    void begin();     // after core.begin + hw.begin
    void tick();      // ~1 s, after hw.tick()
    void fastTick();  // ~100 ms, BEFORE hw.fastTick()

    AsyncWebServer& server() { return _web.server(); }
    AdminAuth& adminAuth() { return _adminAuth; }
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

    char _webVersion[VERSION_TEXT_LEN + 1] = {};
    char _clientId[CLIENT_ID_MAX + 1] = {};
    char _jsonBuf[JSON_SNAPSHOT_CAP] = {};  // loop-task scratch for snapshot rebuilds
    bool _active = false;  // runtime begun with a valid identity (Wi-Fi initialised)
};
