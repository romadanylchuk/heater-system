#include "WebServices.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <BackupCodec.h>
#include <WebAccess.h>
#include <WebJson.h>

namespace {

// Single-instance document storage (static RAM, no heap).
char g_stateBuf[WEB_STATE_CAP];
char g_sensorsBuf[WEB_SENSORS_CAP];
char g_configBuf[WEB_CONFIG_CAP];
char g_logBuf[WEB_LOG_CAP];
char g_scratch[WEB_SCRATCH_CAP];   // loop task only

static_assert(WEB_SCRATCH_CAP >= WEB_STATE_CAP && WEB_SCRATCH_CAP >= WEB_SENSORS_CAP &&
                  WEB_SCRATCH_CAP >= WEB_CONFIG_CAP && WEB_SCRATCH_CAP >= WEB_LOG_CAP,
    "the rebuild scratch must hold every document");

}  // namespace

WebServices::WebServices(CommonState& state, CoreServices& core, ConnectivityServices& net,
    const ConfigSchema& schema, const HwProjectConfig& hwCfg)
    : _state(state),
      _core(core),
      _net(net),
      _schema(schema),
      _hwCfg(hwCfg),
      _gate(net.adminAuth()),
      _stateJson(g_stateBuf, sizeof(g_stateBuf)),
      _sensorsJson(g_sensorsBuf, sizeof(g_sensorsBuf)),
      _configJson(g_configBuf, sizeof(g_configBuf)),
      _logJson(g_logBuf, sizeof(g_logBuf)) {}

uint64_t WebServices::nowMs() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

void WebServices::attach() {
    _net.setWebLayer(&_gate, &WebServices::install, this);
    _core.setCommandResultHook(&WebServices::onCommandResult, this);
}

void WebServices::install(AsyncWebServer& server, WebInstallPhase phase, void* ctx) {
    WebServices* self = static_cast<WebServices*>(ctx);
    if (self == nullptr) {
        return;
    }
    if (phase == WebInstallPhase::Early) {
        self->installEarly(server);
        return;
    }
    bool mounted = LittleFS.begin(false);  // never format: a missing image is served by the rescue page
    bool image = mounted && LittleFS.exists("/index.html.gz");
    self->_webImage.store(image);
    Serial.printf("[web] LittleFS %s, SPA image %s\n", mounted ? "mounted" : "not mounted",
        image ? "present" : "missing (rescue page at /)");
    self->installRead(server);
    self->installWrite(server);
    self->installStatic(server);
    self->_installed.store(true);
}

uint32_t WebServices::nextCmdId() {
    uint32_t n = (_cmdCounter.fetch_add(1) + 1) & ~WEB_CMD_ID_BASE;
    if (n == 0) {
        n = (_cmdCounter.fetch_add(1) + 1) & ~WEB_CMD_ID_BASE;  // skip the wrap to 0
    }
    return WEB_CMD_ID_BASE | n;
}

bool WebServices::postCommand(const Command& c) {
    portENTER_CRITICAL(&_mux);
    _board.expect(c.id);
    portEXIT_CRITICAL(&_mux);
    return _core.commands().post(c);
}

bool WebServices::formatLocal(uint32_t utc, char* out, size_t cap, void* ctx) {
    WebServices* self = static_cast<WebServices*>(ctx);
    return self != nullptr && self->_core.time().formatLocal(utc, out, cap);
}

void WebServices::onCommandResult(const Command& cmd, CommandStatus st, void* ctx) {
    WebServices* self = static_cast<WebServices*>(ctx);
    if (self == nullptr || (cmd.id & WEB_CMD_ID_BASE) == 0) {
        return;  // not a web command
    }
    bool hasValue = false;
    float value = 0.0f;
    if (cmd.type == CommandType::SetNumber &&
        (st == CommandStatus::Ok || st == CommandStatus::Clamped || st == CommandStatus::Unchanged)) {
        hasValue = true;
        value = self->_core.config().getNumber(cmd.settingIndex);  // the applied (possibly clamped) value
    }
    portENTER_CRITICAL(&self->_mux);
    self->_board.record(cmd.id, st, hasValue, value);
    portEXIT_CRITICAL(&self->_mux);
}

bool WebServices::logNeedsRebuild(uint64_t now) const {
    if (!_logBuilt) {
        return true;
    }
    return _core.events().nextSeq() != _lastLogSeq || _state.time.valid != _lastLogTimeValid ||
           now - _lastLogBuildMs >= LOG_REBUILD_MS;
}

bool WebServices::publish(SharedJsonBuffer& buf, size_t len, const char* name, bool& failed) {
    bool ok = buf.update(g_scratch, len);
    if (!ok && !failed && len == 0) {
        Serial.printf("[web] %s document overflow, keeping the previous one\n", name);
    }
    failed = !ok && len == 0;   // a mutex timeout is transient, not an overflow
    return ok;
}

void WebServices::rebuildSnapshots() {
    WebJsonContext c{_schema.controllerType, &_hwCfg, &WebServices::formatLocal, this, nullptr, nullptr};
    // Each builder gets its own document's cap, so a result always fits it.
    publish(_stateJson, buildStateJson(_state, c, g_scratch, WEB_STATE_CAP), "state", _stateFailed);
    publish(_sensorsJson, buildSensorsJson(_state, _hwCfg, g_scratch, WEB_SENSORS_CAP), "sensors", _sensorsFailed);
    publish(_configJson, buildConfigValuesJson(_core.config(), g_scratch, WEB_CONFIG_CAP), "config", _configFailed);

    uint64_t now = nowMs();
    if (logNeedsRebuild(now)) {
        uint32_t seq = _core.events().nextSeq();
        bool timeValid = _state.time.valid;
        size_t len = buildLogJson(_core.events(), c, g_scratch, WEB_LOG_CAP);
        // Only a published document counts as built: a rejected update is
        // retried next tick instead of freezing /api/log (review-4 Must-fix).
        if (publish(_logJson, len, "log", _logFailed)) {
            _lastLogSeq = seq;
            _lastLogTimeValid = timeValid;
            _lastLogBuildMs = now;
            _logBuilt = true;
        }
    }
}

bool WebServices::otaBusyNow() const {
    return _otaBusy.load() || _net.webOtaStarts() != _otaStartsAcked.load() || _net.espotaActive();
}

void WebServices::latchWebImageAfterOta() {
    // Only a filesystem-mode web OTA can have touched LittleFS: a failed or
    // aborted firmware-only update keeps the SPA (review-5 suggestion).
    if (_webImageLatched || _net.webOtaStarts() == 0 || !_net.webOtaFsStarted() || otaBusyNow()) {
        return;
    }
    _webImageLatched = true;
    if (_webImage.exchange(false)) {
        Serial.println("[web] web OTA ended without a reboot: SPA image disabled until reboot (rescue page at /)");
    }
}

void WebServices::exportTick(uint64_t now) {
    portENTER_CRITICAL(&_mux);
    bool build = _export.needsBuild();
    portEXIT_CRITICAL(&_mux);
    if (build) {
        // Built here on the loop task: ConfigEngine is never read on AsyncTCP (D9).
        char* buf = static_cast<char*>(malloc(BACKUP_MAX_BYTES));
        size_t len = 0;
        if (buf != nullptr) {
            len = BackupCodec::exportJson(_core.config(), _core.fwVersion(), buf, BACKUP_MAX_BYTES);
            if (len == 0) {
                free(buf);
                buf = nullptr;
            }
        }
        if (buf == nullptr) {
            if (!_exportFailLogged) {   // once until an export succeeds again
                _exportFailLogged = true;
                Serial.println("[web] backup export failed (no memory or engine not ready)");
            }
        } else {
            _exportFailLogged = false;
        }
        portENTER_CRITICAL(&_mux);
        _export.publish(buf, len, now);   // nullptr: back to Idle, GET answers 404
        portEXIT_CRITICAL(&_mux);
    }
    portENTER_CRITICAL(&_mux);
    char* old = _export.expire(now);
    portEXIT_CRITICAL(&_mux);
    free(old);   // outside the critical section (free(nullptr) is a no-op)
}

void WebServices::tick() {
    if (!_installed.load()) {
        return;
    }
    // Starts counted before the previous tick() were consumed by this tick's
    // net.tick() (it runs first), so they are reflected in ota.inProgress:
    // publish _otaBusy first, then acknowledge them. Later starts keep static
    // serving off through otaBusyNow() until the following tick.
    _otaBusy.store(_state.ota.inProgress);
    _otaStartsAcked.store(_otaStartsPrev);
    _otaStartsPrev = _net.webOtaStarts();
    rebuildSnapshots();
    latchWebImageAfterOta();
    _captive.update(_state.network.setupApActive, _state.network.apIp);
    exportTick(nowMs());
}

void WebServices::fastTick() {
    _captive.process();
}
