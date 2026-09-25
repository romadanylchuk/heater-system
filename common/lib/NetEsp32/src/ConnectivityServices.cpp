#include "ConnectivityServices.h"
#include <Arduino.h>
#include <CommonSettings.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <NetJson.h>
#include <VersionInfo.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

ConnectivityServices::ConnectivityServices(CommonState& state, CoreServices& core, HardwareServices& hw,
    const NetIdentity& id, const HwProjectConfig& hwCfg, const HaCustomEntity* custom, size_t customCount)
    : _state(state),
      _core(core),
      _hw(hw),
      _id(id),
      _hwCfg(hwCfg),
      _custom(custom),
      _customCount(customCount),
      _runtime(state, core.config(), core.events(), _wifi, _mqtt, _ota, _otaStore, _signals) {}

const char* ConnectivityServices::webPass() const {
    return _core.config().getText(commonIndex(CommonSetting::WebPass));
}

void ConnectivityServices::readWebVersion() {
    _webVersion[0] = '\0';
    // Read-only style mount: never format; unmounted again so a later LittleFS
    // OTA never writes under a mount (D19). Stage 05 re-mounts.
    if (!LittleFS.begin(false)) {
        return;
    }
    File f = LittleFS.open("/version.txt", "r");
    if (f) {
        char raw[WEB_VERSION_FILE_MAX];
        size_t n = f.read(reinterpret_cast<uint8_t*>(raw), sizeof(raw));
        f.close();
        if (!parseWebVersionText(raw, n, _webVersion, sizeof(_webVersion))) {
            _webVersion[0] = '\0';
        }
    }
    LittleFS.end();
}

void ConnectivityServices::buildClientId() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(_clientId, sizeof(_clientId), "%s-%02x%02x%02x", _id.prefix != nullptr ? _id.prefix : "", mac[3],
        mac[4], mac[5]);
}

void ConnectivityServices::applyAdminCreds() {
    _adminAuth.set(_core.config().getText(commonIndex(CommonSetting::WebUser)), webPass());
}

void ConnectivityServices::applyPendingWifiCreds() {
    // Loop task. Both settings are written in this one call, before
    // _runtime.tick() reads them, so the Wi-Fi supervisor only ever sees the
    // complete new pair (review-9 Must-fix 2). The handler already validated
    // the pair against the descriptor limits (checkWifiCreds); order does not
    // matter here because nothing reads the settings in between.
    WifiCreds creds;
    if (!_wifiCreds.take(creds)) {
        return;
    }
    uint64_t now = _core.time().monoMs();
    ConfigEngine& cfg = _core.config();
    ConfigStatus passSt = cfg.setText(commonIndex(CommonSetting::WifiPass), creds.pass, EventReason::Web, now);
    ConfigStatus ssidSt = cfg.setText(commonIndex(CommonSetting::WifiSsid), creds.ssid, EventReason::Web, now);
    memset(creds.pass, 0, sizeof(creds.pass));
    Serial.printf("[net] wifi creds applied pass=%u ssid=%u\n", static_cast<unsigned>(passSt),
        static_cast<unsigned>(ssidSt));
}

void ConnectivityServices::begin() {
    readWebVersion();
    buildClientId();
    _otaStore.open();

    uint64_t now = _core.time().monoMs();
    NetBeginStatus st =
        _runtime.begin(_id, _hwCfg, _custom, _customCount, _core.fwVersion(), _webVersion, _clientId, now);
    Serial.printf("[net] begin=%u client=%s web=%s entities=%u mqttAllowed=%u\n", static_cast<unsigned>(st),
        _clientId, _webVersion[0] != '\0' ? _webVersion : "-", static_cast<unsigned>(_runtime.registry().count()),
        _runtime.mqttAllowed() ? 1u : 0u);
    if (st == NetBeginStatus::InvalidIdentity) {
        return;  // inert: Wi-Fi was never initialised, so no server/espota either
    }
    _active = true;

    _mqtt.begin(&_runtime.registry(), _id.prefix, _core.commands(), _signals);
    applyAdminCreds();
    refreshSnapshots();
    _web.begin(_adminAuth, _statusJson, _scanJson, _versionJson, _signals, _wifiCreds, _gate, _installer, _installerCtx);
    _core.setEventPublishHook(&ConnectivityRuntime::eventHook, &_runtime);
}

void ConnectivityServices::tick() {
    if (!_active) {
        return;
    }
    applyPendingWifiCreds();
    NetTickEvents ev = _runtime.tick(_core.time().monoMs());
    if (ev.wifiUp) {
        _core.time().onNetworkUp();
        if (!_espota.started()) {
            _espota.start(_id.prefix, webPass(), _signals, &ConnectivityServices::onEspotaStart, this);
        }
    }
    if (ev.wifiDown) {
        _core.time().onNetworkDown();
    }
    if (ev.adminCredsChanged) {
        applyAdminCreds();
        _espota.restart(webPass());  // loop task: cannot overlap a session (D16)
    }
    refreshSnapshots();
}

void ConnectivityServices::fastTick() {
    if (!_active) {
        return;
    }
    // Espota is not polled while a web OTA session is active, so it can never
    // race ElegantOTA for the global Update object (Update.begin would also
    // refuse; EspotaService then reports nothing for OTA_BEGIN_ERROR).
    if (_runtime.activeOtaSource() != OtaSource::Web) {
        _espota.handle();  // may block for a whole espota upload (watchdog fed by progress)
    }
    ElegantOTA.loop();
    uint64_t now = _core.time().monoMs();  // read after a possibly long handle()
    _runtime.fastTick(now);
    bool inhibit = _runtime.outputsInhibited();
    if (_hw.runtime().outputsInhibited() != inhibit) {
        _hw.runtime().setOutputsInhibited(inhibit, now);
    }
}

void ConnectivityServices::refreshSnapshots() {
    // _jsonBuf is JSON_SNAPSHOT_CAP (1536) >= 320 B (review-6 worst-case status).
    if (buildWifiStatusJson(_state, _jsonBuf, sizeof(_jsonBuf)) > 0) {
        _statusJson.update(_jsonBuf);
    }
    if (buildScanJson(_runtime.scanList(), _state.network.scanRunning, _jsonBuf, sizeof(_jsonBuf)) > 0) {
        _scanJson.update(_jsonBuf);
    }
    if (buildVersionJson(_state, _id.prefix, _jsonBuf, sizeof(_jsonBuf)) > 0) {
        _versionJson.update(_jsonBuf);
    }
}

void ConnectivityServices::onEspotaStart(void* ctx) {
    // Loop task, inside EspotaService::handle(): the loop cannot reach
    // hw.fastTick() until the upload ends, so switch the relays OFF and write
    // the port right now (D14).
    auto* self = static_cast<ConnectivityServices*>(ctx);
    self->_hw.runtime().setOutputsInhibited(true, self->_core.time().monoMs());
    self->_hw.fastTick();
}
