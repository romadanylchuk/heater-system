#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <HwConfig.h>
#include "EventEntry.h"
#include "EventTypes.h"
#include "HaDiscovery.h"
#include "HaEntityRegistry.h"
#include "MqttPublisher.h"
#include "MqttSession.h"
#include "MqttTransport.h"
#include "NetIdentity.h"
#include "NetSignals.h"
#include "OtaBoot.h"
#include "OtaGuard.h"
#include "OtaPlatform.h"
#include "VersionInfo.h"
#include "WifiPort.h"
#include "WifiScanList.h"
#include "WifiSupervisor.h"

// Pure connectivity orchestrator (D2, phase 7): drives WifiSupervisor,
// MqttSession/MqttPublisher, OtaGuard/OtaBoot's OtaMarkerStore/
// OtaHealthMonitor through the WifiPort/MqttTransport/OtaPlatform/KvStore
// ports, so the whole flow (AP -> join -> MQTT session -> discovery -> OTA ->
// reboot request) is native-tested with fakes. Owns no hardware/network
// includes; the caller (NetEsp32's facade, a later phase) wires the real
// adapters and drives begin()/tick() (~1 s)/fastTick() (~100 ms) from the
// loop task, plus onEvent() as EventLog's publish hook target.
enum class NetBeginStatus : uint8_t { Ok, InvalidIdentity, RegistryError };

constexpr uint32_t OTA_REBOOT_DELAY_MS = 1500;

// Grace after a Web OTA start signal before fastTick() checks that
// ElegantOTA's Update.begin() actually opened a session (final-check S3).
// ElegantOTA calls onStart just BEFORE Update.begin(), on another task, so the
// check must not run on the drain that sees the start.
constexpr uint32_t OTA_WEB_BEGIN_CHECK_MS = 500;

struct NetTickEvents {
    bool wifiUp = false;
    bool wifiDown = false;
    bool adminCredsChanged = false;
};

class ConnectivityRuntime {
public:
    ConnectivityRuntime(CommonState& state, ConfigEngine& config, EventSink& events, WifiPort& wifi,
        MqttTransport& mqtt, OtaPlatform& ota, KvStore& otaStore, NetSignals& signals);

    // Validates id; on InvalidIdentity the runtime stays inert (no port is
    // ever touched, control is unaffected). Classifies the OTA boot, inits
    // Wi-Fi and the setup AP/reconnect, builds the HA entity registry
    // (non-Ok -> RegistryError, and MQTT is never enabled, but Wi-Fi/OTA
    // still run), binds the publisher/session and caches the admin creds.
    NetBeginStatus begin(const NetIdentity& id, const HwProjectConfig& hw, const HaCustomEntity* custom,
        size_t customCount, const char* fwVersion, const char* webVersion, const char* clientId, uint64_t nowMs);

    // ~1 s, loop task: Wi-Fi scan/supervisor, MQTT session lifecycle,
    // inbound-command republish/drop reporting, publisher refresh, OTA
    // health confirmation/rollback-arming, admin-creds change detection, and
    // the CommonState network/ota status snapshot.
    NetTickEvents tick(uint64_t nowMs);

    // ~100 ms, loop task: drains the OTA signal atomics into OtaGuard, saves
    // the OTA marker and requests a reboot on success, and pumps the MQTT
    // publish pipeline while a session is up and no OTA is active.
    // Web OTA robustness (final-check S3): a Web start with no progress whose
    // Update session is not running OTA_WEB_BEGIN_CHECK_MS later (ElegantOTA's
    // Update.begin() failed and it sends no onEnd) fails the session at once
    // instead of holding relays OFF until the 60 s stall; and every failed or
    // stalled Web session aborts the shared Update object so the next OTA can
    // begin without a reboot.
    void fastTick(uint64_t nowMs);

    // EventLog's publish hook target: enqueues the event to the MQTT
    // publisher while a session is active.
    void onEvent(const EventEntry& e);
    static void eventHook(const EventEntry& e, void* ctx);

    // OTA guard active, or a health-timeout rollback is pending: the caller
    // must keep relay outputs inhibited. The rollback flag is never cleared --
    // only an actual reboot ends it (fail closed; see tick()).
    bool outputsInhibited() const { return _otaGuard.active() || _rollbackPending; }

    // Source of the OTA session OtaGuard currently tracks (None when idle).
    // The facade skips espota polling while a Web session is active.
    OtaSource activeOtaSource() const { return _otaGuard.active() ? _otaGuard.source() : OtaSource::None; }

    const HaEntityRegistry& registry() const { return _registry; }
    const WifiScanList& scanList() const { return _scanList; }
    const NetIdentity& identity() const { return _identity; }
    bool mqttAllowed() const { return _mqttAllowed; }

private:
    static constexpr size_t ADMIN_USER_LEN = 32;
    static constexpr size_t ADMIN_PASS_LEN = 64;

    void applyWifiActions(const WifiActions& a, const char* ssid, const char* pass);
    void fillStatus(uint64_t nowMs);
    void cacheAdminCreds();

    CommonState& _state;
    ConfigEngine& _config;
    EventSink& _events;
    WifiPort& _wifi;
    MqttTransport& _mqtt;
    OtaPlatform& _ota;
    KvStore& _otaStore;
    NetSignals& _signals;

    OtaMarkerStore _marker;
    WifiSupervisor _supervisor;
    MqttSession _session;
    MqttPublisher _publisher;
    HaEntityRegistry _registry;
    OtaGuard _otaGuard;
    OtaHealthMonitor _health;
    WifiScanList _scanList;

    NetIdentity _identity{};
    const char* _clientId = nullptr;
    char _willTopic[HA_TOPIC_MAX] = {};
    bool _mqttAllowed = false;

    char _cachedWebUser[ADMIN_USER_LEN + 1] = {};
    char _cachedWebPass[ADMIN_PASS_LEN + 1] = {};

    uint32_t _lastProgressSeen = 0;
    bool _webBeginCheckPending = false;  // Web start seen, Update-running check not done yet
    uint64_t _webStartMs = 0;
    uint32_t _lastSeenDroppedCommands = 0;
    bool _rollbackPending = false;
    bool _rollbackFailLogged = false;  // DiagnosticWarning for a returned rollbackAndReboot() logged once
    bool _started = false;  // begin() passed identity validation; false = inert (never touch ports)
};
