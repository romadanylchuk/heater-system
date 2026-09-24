#include "ConnectivityRuntime.h"
#include <string.h>
#include "CommonSettings.h"

namespace {
// DiagnosticWarning(DIAG_CODE_OTA_HEALTH_TIMEOUT) value: 0 = health check timed
// out (rollback armed); 1 = rollbackAndReboot() returned, falling back to restart().
constexpr float OTA_ROLLBACK_FAILED_DIAG_VALUE = 1.0f;

inline const char* textOrEmpty(const ConfigEngine& config, size_t index) {
    const char* v = config.getText(index);
    return v != nullptr ? v : "";
}
}  // namespace

ConnectivityRuntime::ConnectivityRuntime(CommonState& state, ConfigEngine& config, EventSink& events, WifiPort& wifi,
    MqttTransport& mqtt, OtaPlatform& ota, KvStore& otaStore, NetSignals& signals)
    : _state(state),
      _config(config),
      _events(events),
      _wifi(wifi),
      _mqtt(mqtt),
      _ota(ota),
      _otaStore(otaStore),
      _signals(signals),
      _marker(otaStore) {}

void ConnectivityRuntime::applyWifiActions(const WifiActions& a, const char* ssid, const char* pass) {
    if (a.disconnect) {
        _wifi.disconnect();
    }
    if (a.stopAp) {
        _wifi.stopAp();
    }
    if (a.startAp) {
        _wifi.startAp(_identity.apSsid);
    }
    if (a.startScan) {
        _wifi.startScan();
    }
    if (a.connect) {
        _wifi.connect(ssid, pass);
    }
}

void ConnectivityRuntime::cacheAdminCreds() {
    const char* user = textOrEmpty(_config, commonIndex(CommonSetting::WebUser));
    const char* pass = textOrEmpty(_config, commonIndex(CommonSetting::WebPass));
    strncpy(_cachedWebUser, user, sizeof(_cachedWebUser) - 1);
    _cachedWebUser[sizeof(_cachedWebUser) - 1] = '\0';
    strncpy(_cachedWebPass, pass, sizeof(_cachedWebPass) - 1);
    _cachedWebPass[sizeof(_cachedWebPass) - 1] = '\0';
}

void ConnectivityRuntime::fillStatus(uint64_t nowMs) {
    NetworkStatus& net = _state.network;

    net.wifiMode = _supervisor.mode();
    net.wifiConnected = _supervisor.linkUp();
    if (net.wifiConnected) {
        net.wifiRssi = _wifi.rssi();
        _wifi.localIp(net.ip, sizeof(net.ip));
    } else {
        net.wifiRssi = 0;
        net.ip[0] = '\0';
    }

    const char* ssid = textOrEmpty(_config, commonIndex(CommonSetting::WifiSsid));
    strncpy(net.ssid, ssid, sizeof(net.ssid) - 1);
    net.ssid[sizeof(net.ssid) - 1] = '\0';
    strncpy(net.hostname, _identity.prefix, sizeof(net.hostname) - 1);
    net.hostname[sizeof(net.hostname) - 1] = '\0';

    net.setupApActive = _supervisor.apActive();
    if (net.setupApActive) {
        strncpy(net.apSsid, _identity.apSsid, sizeof(net.apSsid) - 1);
        net.apSsid[sizeof(net.apSsid) - 1] = '\0';
        _wifi.apIp(net.apIp, sizeof(net.apIp));
    } else {
        net.apSsid[0] = '\0';
        net.apIp[0] = '\0';
    }

    net.wifiDownS = _supervisor.outageS(nowMs);
    net.wifiConnectCount = _supervisor.connectCount();
    net.scanRunning = _supervisor.scanRunning();
    net.scanCount = static_cast<uint8_t>(_scanList.count());

    net.mqttEnabled = _session.enabled();
    net.mqttConnected = _session.connected();
    net.mqttConnectCount = _session.connectCount();
    net.mqttDroppedCommands = _signals.droppedCommands.load();

    OtaStatus& ota = _state.ota;
    ota.inProgress = _otaGuard.active();
    ota.source = _otaGuard.source();
    ota.pendingVerify = _health.pending();
    ota.verifyRemainingS = _health.remainingS(nowMs);
}

NetBeginStatus ConnectivityRuntime::begin(const NetIdentity& id, const HwProjectConfig& hw,
    const HaCustomEntity* custom, size_t customCount, const char* fwVersion, const char* webVersion,
    const char* clientId, uint64_t nowMs) {
    if (!validateNetIdentity(id)) {
        return NetBeginStatus::InvalidIdentity;
    }
    _started = true;
    _identity = id;
    _clientId = clientId;

    fillVersionStatus(_state.versions, fwVersion, webVersion);

    // --- OTA boot classification (D15) ---
    char markerBuf[OTA_LABEL_MAX + 1] = {};
    bool markerPresent = _marker.load(markerBuf, sizeof(markerBuf));
    char runningLabelBuf[OTA_LABEL_MAX + 1] = {};
    _ota.runningLabel(runningLabelBuf, sizeof(runningLabelBuf));
    OtaBootInfo info{_ota.runningImageState(), runningLabelBuf, markerPresent, markerPresent ? markerBuf : nullptr};
    OtaBootOutcome outcome = classifyOtaBoot(info);
    if (outcome == OtaBootOutcome::RolledBack) {
        _events.logEvent(toU16(EventType::OtaRollback), EVENT_SOURCE_OTA, 0, 0, EventReason::Boot);
        _marker.clear();
    } else if (outcome == OtaBootOutcome::UpdatedNoRollback) {
        _marker.clear();
    }
    _health.begin(outcome == OtaBootOutcome::Trial, nowMs, _state.oneWire.readCycleCount);
    _state.ota.bootOutcome = outcome;
    _state.ota.pendingVerify = (outcome == OtaBootOutcome::Trial);

    // --- Wi-Fi ---
    _wifi.init(_identity.prefix);
    const char* ssid = textOrEmpty(_config, commonIndex(CommonSetting::WifiSsid));
    const char* pass = textOrEmpty(_config, commonIndex(CommonSetting::WifiPass));
    WifiActions wActions = _supervisor.begin(ssid, pass, nowMs);
    applyWifiActions(wActions, ssid, pass);

    // --- HA registry / MQTT setup ---
    HaRegistryStatus regStatus = _registry.build(_config, hw, custom, customCount);
    _mqttAllowed = (regStatus == HaRegistryStatus::Ok);

    buildAvailabilityTopic(_identity, _willTopic, sizeof(_willTopic));
    _publisher.begin(_registry, _identity, fwVersion);

    MqttEndpoint ep{};
    if (_mqttAllowed) {
        mqttEndpointFromConfig(_config, ep);
    }
    _session.begin(ep, nowMs);

    // --- Admin creds (D21 baseline) ---
    cacheAdminCreds();

    fillStatus(nowMs);

    return _mqttAllowed ? NetBeginStatus::Ok : NetBeginStatus::RegistryError;
}

NetTickEvents ConnectivityRuntime::tick(uint64_t nowMs) {
    if (!_started) {
        return NetTickEvents{};
    }
    if (_rollbackPending) {
        // Fail closed: _rollbackPending (and so outputsInhibited()) stays set
        // until the device actually reboots. One attempt per slow tick; the
        // status snapshot (step 8) is intentionally not refreshed meanwhile.
        _ota.rollbackAndReboot();  // normally never returns
        // Returned: rollback failed (no valid slot / ota_data write error).
        // Plain reboot instead -- the image is still pending-verify, so the
        // bootloader rolls it back / does not re-confirm it.
        if (!_rollbackFailLogged) {
            _events.logEvent(toU16(EventType::DiagnosticWarning),
                EVENT_SOURCE_DIAG_BASE + DIAG_CODE_OTA_HEALTH_TIMEOUT, OTA_ROLLBACK_FAILED_DIAG_VALUE, 0,
                EventReason::Logic);
            _rollbackFailLogged = true;
        }
        _ota.restart();  // never returns on real hardware; retried next tick if it does
        return NetTickEvents{};
    }

    NetTickEvents events{};

    // 1. Scan.
    bool scanFinished = false;
    if (_supervisor.scanRunning()) {
        int status = _wifi.scanStatus();
        if (status >= 0) {
            _scanList.clear();
            for (int i = 0; i < status; ++i) {
                WifiScanEntry e{};
                if (_wifi.scanEntry(i, e)) {
                    _scanList.add(e.ssid, e.rssi, e.secure);
                }
            }
            _wifi.scanClear();
            scanFinished = true;
        } else if (status == WIFI_SCAN_FAILED_STATUS) {
            scanFinished = true;
        }
    }

    // 2. Wi-Fi supervisor.
    const char* ssid = textOrEmpty(_config, commonIndex(CommonSetting::WifiSsid));
    const char* pass = textOrEmpty(_config, commonIndex(CommonSetting::WifiPass));
    WifiInputs wifiIn{};
    wifiIn.ssid = ssid;
    wifiIn.pass = pass;
    wifiIn.linkUp = _wifi.linkUp();
    wifiIn.apUp = _wifi.apUp();
    wifiIn.scanRequested = _signals.scanRequested.exchange(false);
    wifiIn.scanFinished = scanFinished;

    WifiActions wActions = _supervisor.tick(wifiIn, nowMs, _events);
    applyWifiActions(wActions, ssid, pass);
    if (wActions.linkWentUp) {
        _wifi.startMdns(_identity.prefix);
        events.wifiUp = true;
    }
    if (wActions.linkWentDown) {
        events.wifiDown = true;
    }

    // 3. MQTT session.
    MqttEndpoint ep{};
    if (_mqttAllowed) {
        mqttEndpointFromConfig(_config, ep);
    }
    MqttSessionActions mActions = _session.tick(ep, _supervisor.linkUp(), _mqtt.connected(), nowMs, _events);
    if (mActions.disconnect) {
        if (_publisher.sessionActive()) {
            _publisher.publishOffline(_mqtt);
        }
        _mqtt.disconnect(false);
    }
    if (mActions.forceDisconnect) {
        _mqtt.disconnect(true);
    }
    if (mActions.configure) {
        _mqtt.configure(ep, _clientId, _willTopic);
    }
    if (mActions.connect) {
        _mqtt.connect();
    }
    if (mActions.sessionStarted) {
        _publisher.onSessionStart(nowMs);
        _publisher.refresh(_state, _config, nowMs);
    }
    if (mActions.sessionEnded) {
        _publisher.onSessionEnd();
    }

    // 4. Inbound-command republish + dropped-command reporting.
    uint32_t bits[4] = {};
    if (_signals.republish.takeAll(bits)) {
        for (size_t w = 0; w < 4; ++w) {
            for (size_t b = 0; b < 32; ++b) {
                if ((bits[w] & (1u << b)) != 0) {
                    _publisher.markDirty(w * 32 + b);
                }
            }
        }
    }
    uint32_t dropped = _signals.droppedCommands.load();
    if (dropped > _lastSeenDroppedCommands) {
        _events.logEvent(toU16(EventType::DiagnosticWarning), EVENT_SOURCE_DIAG_BASE + DIAG_CODE_MQTT_CMD_DROPPED,
            static_cast<float>(dropped), 0, EventReason::Logic);
        _lastSeenDroppedCommands = dropped;
    }

    // 5. Publisher refresh.
    _publisher.refresh(_state, _config, nowMs);

    // 6. OTA health.
    OtaHealthMonitor::Verdict verdict = _health.tick(nowMs, _state.oneWire.readCycleCount);
    if (verdict == OtaHealthMonitor::Verdict::Confirm) {
        _ota.markRunningValid();
        _marker.clear();
        _events.logEvent(toU16(EventType::OtaUpdate), EVENT_SOURCE_OTA, OTA_EVENT_CONFIRMED, 0, EventReason::Logic);
    } else if (verdict == OtaHealthMonitor::Verdict::Rollback) {
        _events.logEvent(
            toU16(EventType::OtaUpdate), EVENT_SOURCE_OTA, OTA_EVENT_HEALTH_TIMEOUT, 0, EventReason::Logic);
        _events.logEvent(toU16(EventType::DiagnosticWarning), EVENT_SOURCE_DIAG_BASE + DIAG_CODE_OTA_HEALTH_TIMEOUT,
            0, 0, EventReason::Logic);
        _rollbackPending = true;  // outputsInhibited() true from here on; rollbackAndReboot() from the next tick()
    }

    // 7. Admin creds change detection (D21).
    const char* webUser = textOrEmpty(_config, commonIndex(CommonSetting::WebUser));
    const char* webPass = textOrEmpty(_config, commonIndex(CommonSetting::WebPass));
    if (strcmp(webUser, _cachedWebUser) != 0 || strcmp(webPass, _cachedWebPass) != 0) {
        cacheAdminCreds();
        events.adminCredsChanged = true;
    }

    // 8. Status snapshot.
    fillStatus(nowMs);

    return events;
}

void ConnectivityRuntime::fastTick(uint64_t nowMs) {
    if (!_started) {
        return;
    }
    OtaSignalSnapshot snap = takeOtaSignals(_signals, _lastProgressSeen);

    // Web begin check (S3). Progress or an end proves/settles the session, so
    // only a silent Web start is checked, once, after the grace period.
    if (snap.started != OtaSource::None) {
        _webBeginCheckPending = (snap.started == OtaSource::Web);
        _webStartMs = nowMs;
    }
    if (snap.progressed || snap.ended != 0) {
        _webBeginCheckPending = false;
    }
    if (_webBeginCheckPending && nowMs - _webStartMs >= OTA_WEB_BEGIN_CHECK_MS) {
        _webBeginCheckPending = false;
        if (_otaGuard.active() && _otaGuard.source() == OtaSource::Web && !_ota.updateRunning()) {
            snap.ended = 2;  // Update.begin() failed: fail now, not after the 60 s stall
        }
    }

    OtaGuardResult r = _otaGuard.update(snap, nowMs, _events);
    if (r.failed && r.source == OtaSource::Web) {
        // Failed or stalled web session: nothing writes Update any more (the
        // request finished, or no chunk arrived for 60 s), so close it here
        // on the loop task; otherwise every later Update.begin() is refused.
        _webBeginCheckPending = false;
        _ota.abortUpdate();
    }
    if (r.succeeded) {
        char bootLabelBuf[OTA_LABEL_MAX + 1] = {};
        char runningLabelBuf[OTA_LABEL_MAX + 1] = {};
        bool haveBoot = _ota.bootLabel(bootLabelBuf, sizeof(bootLabelBuf));
        bool haveRunning = _ota.runningLabel(runningLabelBuf, sizeof(runningLabelBuf));
        if (haveBoot && (!haveRunning || strcmp(bootLabelBuf, runningLabelBuf) != 0)) {
            _marker.save(bootLabelBuf);
        }
        _state.system.rebootRequested = true;
        _state.system.rebootAtMs = nowMs + OTA_REBOOT_DELAY_MS;
    }

    if (_publisher.sessionActive() && _mqtt.connected() && !_otaGuard.active()) {
        _publisher.pump(_mqtt, _config);
    }
}

void ConnectivityRuntime::onEvent(const EventEntry& e) {
    if (_started && _publisher.sessionActive()) {
        _publisher.enqueueEvent(e);
    }
}

void ConnectivityRuntime::eventHook(const EventEntry& e, void* ctx) {
    static_cast<ConnectivityRuntime*>(ctx)->onEvent(e);
}
