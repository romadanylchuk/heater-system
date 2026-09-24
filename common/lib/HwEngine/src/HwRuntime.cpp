#include "HwRuntime.h"
#include <BoardConfig.h>
#include <RelayMask.h>
#include "HwSettings.h"

HwRuntime::HwRuntime(CommonState& state, ConfigEngine& config, EventSink& events, RelayPort& port, OneWireBus& bus)
    : _state(state),
      _config(config),
      _events(events),
      _port(port),
      _bus(bus),
      _sensors(_bus, _config, _events),
      _antiSeize(_relays, _k1, _events) {}

HwStatus HwRuntime::begin(const HwProjectConfig& cfg, uint64_t nowMs) {
    _cfg = &cfg;
    _ready = false;

    if (!validateHwConfig(cfg)) {
        return HwStatus::InvalidConfig;
    }

    const int idxLock = _config.indexOf(HW_KEY_RELAY_LOCK);
    const int idxInterval = _config.indexOf(HW_KEY_AS_INTERVAL);
    const int idxTime = _config.indexOf(HW_KEY_AS_TIME);
    const int idxDuration = _config.indexOf(HW_KEY_AS_DURATION);
    if (idxLock < 0 || idxInterval < 0 || idxTime < 0 || idxDuration < 0) {
        return HwStatus::SettingMissing;
    }

    size_t idxEnable[MAX_ANTI_SEIZE_OUTPUTS] = {};
    for (size_t i = 0; i < cfg.antiSeizeCount; ++i) {
        const int idx = _config.indexOf(cfg.antiSeize[i].enableKey);
        if (idx < 0) {
            return HwStatus::SettingMissing;
        }
        const SettingDescriptor* d = _config.descriptor(static_cast<size_t>(idx));
        if (d == nullptr || d->type != SettingType::Bool) {
            return HwStatus::SettingMissing;
        }
        idxEnable[i] = static_cast<size_t>(idx);
    }

    _idxLock = static_cast<size_t>(idxLock);
    _idxInterval = static_cast<size_t>(idxInterval);
    _idxTime = static_cast<size_t>(idxTime);
    _idxDuration = static_cast<size_t>(idxDuration);
    for (size_t i = 0; i < cfg.antiSeizeCount; ++i) {
        _idxEnable[i] = idxEnable[i];
    }

    _relays.configure(cfg.relays, cfg.relayCount, nowMs);
    if (cfg.k1.present) {
        _k1.begin(nowMs);
    }
    if (!_sensors.begin(cfg.sensors, cfg.sensorCount, nowMs)) {
        return HwStatus::SettingMissing;
    }
    _antiSeize.configure(cfg.antiSeize, cfg.antiSeizeCount, nowMs);

    _ready = true;
    // Reconcile a pending pre-ready inhibit request (setOutputsInhibited()
    // only stores the flag while !_ready): RelayBank must reflect it the
    // instant the runtime becomes ready, so normal arbitration never runs
    // even briefly while the system still believes outputs are inhibited.
    _relays.setInhibited(_outputsInhibited, nowMs, _events);
    pushSettings();

    _state.k1.present = cfg.k1.present;

    updateRelayAndK1Status(nowMs);
    _sensors.fillStatus(_state);
    _antiSeize.fillStatus(_state.antiSeize, nowMs);

    return HwStatus::Ok;
}

void HwRuntime::pushSettings() {
    _relays.setLockMs(static_cast<uint32_t>(_config.getInt(_idxLock)) * 1000u);

    AntiSeizeSettings settings{};
    settings.intervalDays = static_cast<uint16_t>(_config.getInt(_idxInterval));
    settings.startMinute = static_cast<uint16_t>(_config.getInt(_idxTime));
    settings.durationS = static_cast<uint16_t>(_config.getInt(_idxDuration));
    for (size_t i = 0; i < _cfg->antiSeizeCount; ++i) {
        settings.enabled[i] = _config.getBool(_idxEnable[i]);
    }
    _antiSeize.setSettings(settings);
}

CommandStatus HwRuntime::handleCommand(Command& cmd, uint64_t nowMs) {
    if (!_ready) {
        return CommandStatus::Rejected;
    }

    switch (cmd.type) {
        case CommandType::AssignSensor:
            if (cmd.settingIndex >= _sensors.count()) {
                return CommandStatus::InvalidCommand;
            }
            return _sensors.assign(static_cast<uint8_t>(cmd.settingIndex), cmd.address, cmd.origin, nowMs);
        case CommandType::ClearSensor:
            if (cmd.settingIndex >= _sensors.count()) {
                return CommandStatus::InvalidCommand;
            }
            return _sensors.clear(static_cast<uint8_t>(cmd.settingIndex), cmd.origin, nowMs);
        case CommandType::RescanOneWire:
            _sensors.requestRescan();
            return CommandStatus::Ok;
        default:
            return CommandStatus::InvalidCommand;
    }
}

CommandStatus HwRuntime::commandHook(Command& cmd, uint64_t monoMs, void* ctx) {
    return static_cast<HwRuntime*>(ctx)->handleCommand(cmd, monoMs);
}

void HwRuntime::tick(uint64_t nowMs, const LocalTimeInfo& local) {
    if (!_ready) {
        return;
    }

    pushSettings();
    _sensors.tick(nowMs);
    if (!_outputsInhibited) {
        _antiSeize.tick(nowMs, local);
    }

    fastTick(nowMs);

    _sensors.fillStatus(_state);
    _antiSeize.fillStatus(_state.antiSeize, nowMs);
}

void HwRuntime::fastTick(uint64_t nowMs) {
    if (_ready && _cfg->k1.present) {
        if (_outputsInhibited) {
            // Skip the K1 tick/replay while inhibited; cancel any still-busy
            // run so it does not resume once the inhibit is released (D13).
            if (_k1.busy()) {
                _k1.cancel(nowMs);
            }
        } else {
            _k1.tick(nowMs);
            _relays.requestControl(_cfg->k1.powerChannel, _k1.powerOn(), RelayReason::K1Drive);
            _relays.requestControl(_cfg->k1.directionChannel, _k1.directionOpen(), RelayReason::K1Drive);
        }
    }
    if (_ready) {
        _relays.update(nowMs, _events);
    }

    const uint8_t byte = _ready ? _relays.outputByte(RELAY_ACTIVE_LOW) : RELAY_ALL_OFF_BYTE;
    const bool reassertDue = _hasWritten && (nowMs - _lastWriteMs >= RELAY_REASSERT_MS);

    if (!_hasWritten || byte != _lastWritten || reassertDue || _ioError) {
        if (_port.write(byte)) {
            _ioError = false;
            _lastWritten = byte;
            _hasWritten = true;
            _lastWriteMs = nowMs;
        } else {
            if (!_ioError) {
                _events.logEvent(toU16(EventType::DiagnosticWarning), EVENT_SOURCE_DIAG_BASE + DIAG_CODE_RELAY_IO, 0,
                    0, EventReason::Logic);
            }
            _ioError = true;
            ++_ioErrorCount;
        }
    }

    updateRelayAndK1Status(nowMs);
}

void HwRuntime::setOutputsInhibited(bool inhibited, uint64_t nowMs) {
    if (_outputsInhibited == inhibited) {
        return;  // idempotent, mirroring RelayBank::setInhibited's own guard
    }
    _outputsInhibited = inhibited;

    if (inhibited) {
        if (_ready && _cfg->k1.present) {
            _k1.cancel(nowMs);
        }
        if (_ready) {
            _relays.setInhibited(true, nowMs, _events);
        }
    } else if (_ready) {
        _relays.setInhibited(false, nowMs, _events);
    }
}

void HwRuntime::updateRelayAndK1Status(uint64_t nowMs) {
    _relays.fillStatus(_state.relays, nowMs);
    _state.relays.ioError = _ioError;
    _state.relays.ioErrorCount = _ioErrorCount;

    _state.k1.present = _ready && _cfg != nullptr && _cfg->k1.present;
    if (_state.k1.present) {
        _state.k1.busy = _k1.busy();
        _state.k1.powerOn = _k1.powerOn();
        _state.k1.directionOpen = _k1.directionOpen();
        _state.k1.currentRunMs = static_cast<uint32_t>(_k1.currentRunMs(nowMs));
    } else {
        _state.k1.busy = false;
        _state.k1.powerOn = false;
        _state.k1.directionOpen = false;
        _state.k1.currentRunMs = 0;
    }
}
