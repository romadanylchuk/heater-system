#include "CoreServices.h"
#include <Arduino.h>

CoreServices::CoreServices(CommonState& state, const ConfigSchema& schema, const char* fwVersion)
    : _state(state),
      _schema(schema),
      _fwVersion(fwVersion != nullptr ? fwVersion : ""),
      _cfgStore(NVS_NS_CONFIG),
      _logStore(NVS_NS_EVENTS),
      _time(),
      _events(_logStore, _time),
      _config(_cfgStore, _events),
      _queue(),
      _runtime(_state, _config, _events, _queue) {}

void CoreServices::applyTimezone(const char* tz, EventReason reason) {
    _time.setTimezone(tz);
    if (_time.tzValid()) {
        _state.diag.tzInvalid = false;
        return;
    }
    _state.diag.tzInvalid = true;
    const uint16_t source =
        static_cast<uint16_t>(EVENT_SOURCE_SETTING_BASE + commonIndex(CommonSetting::Tz));
    _events.logEvent(toU16(EventType::TzInvalid), source, 0.0f, 0.0f, reason);
}

void CoreServices::onSettingChanged(size_t index, void* ctx) {
    CoreServices* self = static_cast<CoreServices*>(ctx);
    if (self == nullptr) {
        return;
    }
    if (index == commonIndex(CommonSetting::Tz)) {
        self->applyTimezone(self->_config.getText(index), EventReason::Logic);
    } else if (index == commonIndex(CommonSetting::NtpServer)) {
        self->_time.setNtpServer(self->_config.getText(index));
    }
}

void CoreServices::begin(TwoWire& wire) {
    // 1. Watchdog (reconfigures/subscribes the loop task).
    if (!Watchdog::begin()) {
        _state.diag.watchdogError = true;
    }

    // 2. Reset cause (captured before anything else can reset the chip again).
    const ResetCause cause = Watchdog::lastResetCause();
    _state.system.lastResetCause = cause;

    // 3. Open the NVS stores.
    _cfgStore.open();
    _logStore.open();

    // 4. RTC read + libc wall clock seed.
    const RtcReadStatus rtcStatus = _time.begin(wire);

    // 5. Event log (loads persisted slots into the RAM ring).
    _events.begin();

    // 6. Reboot event -- logged first so every boot leaves a trace even if a
    // later step (config/DI1) fails.
    const bool isWatchdogReset = cause == ResetCause::TaskWatchdog || cause == ResetCause::InterruptWatchdog ||
        cause == ResetCause::OtherWatchdog;
    _events.logEvent(toU16(EventType::Reboot), isWatchdogReset ? EVENT_SOURCE_WATCHDOG : EVENT_SOURCE_SYSTEM,
        static_cast<float>(static_cast<uint8_t>(cause)), 0.0f, EventReason::Boot);

    // 7. RTC diagnostics.
    _state.diag.rtcMissing = (rtcStatus == RtcReadStatus::Missing);
    _state.diag.rtcInvalid = (rtcStatus == RtcReadStatus::Invalid);
    if (rtcStatus == RtcReadStatus::Missing) {
        _events.logEvent(toU16(EventType::RtcMissing), EVENT_SOURCE_RTC, 0.0f, 0.0f, EventReason::Boot);
    } else if (rtcStatus == RtcReadStatus::Invalid) {
        _events.logEvent(toU16(EventType::RtcInvalid), EVENT_SOURCE_RTC, 0.0f, 0.0f, EventReason::Boot);
    }

    // 8. Settings engine (load/clamp/migrate from NVS).
    _config.begin(_schema, _time.monoMs());

    // 9. DI1 boot countdown -- relays are already OFF (SAFETY statements ran
    // before begin() was called).
    const ResetGatePhase gatePhase = FactoryResetInput::runBootCountdown(wire, _state, _resetHook, _resetHookCtx);
    if (gatePhase == ResetGatePhase::Confirmed) {
        _runtime.performFactoryReset(EventReason::Di1, _time.monoMs());
    }

    // 10. Time zone / NTP server from settings (post-reset, so a confirmed
    // factory reset applies before these are read).
    applyTimezone(_config.getText(commonIndex(CommonSetting::Tz)), EventReason::Boot);
    _time.setNtpServer(_config.getText(commonIndex(CommonSetting::NtpServer)));

    // 11. Live config-change hook for tz/ntpServer.
    _config.setChangeHook(&CoreServices::onSettingChanged, this);

    // 12. Command queue.
    if (!_queue.begin()) {
        _state.diag.commandQueueFull = true;
        Serial.println("[core] command queue allocation failed");
    }

    // 13. Publish the current time status.
    _time.fillStatus(_state.time);

    // 14. One-line boot summary.
    Serial.printf("[core] fw=%s reset=%u timeValid=%u cfgVerAtBoot=%u events=%u\n", _fwVersion,
        static_cast<unsigned>(cause), static_cast<unsigned>(_state.time.valid),
        static_cast<unsigned>(_config.storedVersionAtBoot()), static_cast<unsigned>(_events.count()));
}

void CoreServices::tick() {
    Watchdog::feed();

    const uint64_t mono = _time.monoMs();
    _runtime.tick(mono);
    _time.tick(_events);
    _time.fillStatus(_state.time);

    if (_state.system.rebootRequested && mono >= _state.system.rebootAtMs) {
        _config.flushNow();
        ESP.restart();
    }
}
