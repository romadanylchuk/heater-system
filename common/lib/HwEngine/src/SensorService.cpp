#include "SensorService.h"
#include <math.h>
#include <string.h>

SensorService::SensorService(OneWireBus& bus, ConfigEngine& config, EventSink& events)
    : _bus(bus), _config(config), _events(events) {
}

bool SensorService::begin(const LogicalSensorDesc* sensors, size_t count, uint64_t nowMs) {
    if (count > MAX_LOGICAL_SENSORS) {
        count = MAX_LOGICAL_SENSORS;
    }

    for (size_t i = 0; i < count; ++i) {
        int idx = _config.indexOf(sensors[i].settingKey);
        if (idx < 0) {
            return false;
        }
        const SettingDescriptor* d = _config.descriptor(static_cast<size_t>(idx));
        if (d == nullptr || d->type != SettingType::Text) {
            return false;
        }
        _sensors[i] = Sensor{};
        _sensors[i].settingIndex = idx;
    }
    _count = count;

    _pendingRead = false;
    _everRequested = false;
    _requestMs = 0;
    _lastRequestMs = 0;
    _rescanRequested = false;
    _scanDeviceCount = 0;
    _scanOverflow = false;
    _scanDone = false;
    _scanRunCount = 0;

    syncMapping(nowMs, EventReason::Boot);
    doScan(nowMs);
    evaluateMissing(nowMs);
    return true;
}

void SensorService::tick(uint64_t nowMs) {
    if (_rescanRequested) {
        _rescanRequested = false;
        doScan(nowMs);
        _pendingRead = false;
        _everRequested = false;
    }
    syncMapping(nowMs, EventReason::Logic);
    busCycle(nowMs);
    evaluateMissing(nowMs);
}

void SensorService::requestRescan() {
    _rescanRequested = true;
}

CommandStatus SensorService::assign(uint8_t logical, const uint8_t address[8], EventReason origin, uint64_t nowMs) {
    if (logical >= _count) {
        return CommandStatus::Rejected;
    }
    if (address == nullptr || !Ds18b20::isValidRom(address)) {
        return CommandStatus::Rejected;
    }

    Sensor& target = _sensors[logical];
    if (target.assigned && addressEquals(target.address, address)) {
        return CommandStatus::Unchanged;
    }

    // Move: clear any other logical sensor already holding this address.
    for (size_t i = 0; i < _count; ++i) {
        if (i != logical && _sensors[i].assigned && addressEquals(_sensors[i].address, address)) {
            _config.setText(static_cast<size_t>(_sensors[i].settingIndex), "", origin, nowMs);
        }
    }

    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(address, text);
    _config.setText(static_cast<size_t>(target.settingIndex), text, origin, nowMs);

    syncMapping(nowMs, origin);
    evaluateMissing(nowMs);
    return CommandStatus::Ok;
}

CommandStatus SensorService::clear(uint8_t logical, EventReason origin, uint64_t nowMs) {
    if (logical >= _count) {
        return CommandStatus::Rejected;
    }
    if (!_sensors[logical].assigned) {
        return CommandStatus::Unchanged;
    }
    _config.setText(static_cast<size_t>(_sensors[logical].settingIndex), "", origin, nowMs);
    syncMapping(nowMs, origin);
    evaluateMissing(nowMs);
    return CommandStatus::Ok;
}

SensorState SensorService::state(uint8_t logical) const {
    if (logical >= _count || !_sensors[logical].assigned) {
        return SensorState::Unassigned;
    }
    return _sensors[logical].debounce.state();
}

std::optional<float> SensorService::value(uint8_t logical) const {
    if (logical >= _count || !_sensors[logical].assigned) {
        return std::nullopt;
    }
    return _sensors[logical].debounce.value();
}

bool SensorService::missing(uint8_t logical) const {
    if (logical >= _count) {
        return false;
    }
    return _sensors[logical].missing;
}

void SensorService::fillStatus(CommonState& state) const {
    state.sensors.count = static_cast<uint8_t>(_count);
    uint32_t bits = 0;
    for (size_t i = 0; i < _count; ++i) {
        const Sensor& s = _sensors[i];
        LogicalSensorStatus& ls = state.sensors.sensor[i];
        ls.state = s.assigned ? s.debounce.state() : SensorState::Unassigned;
        std::optional<float> v = s.assigned ? s.debounce.value() : std::nullopt;
        ls.tempC = v.has_value() ? *v : NAN;
        ls.assigned = s.assigned;
        ls.missing = s.missing;
        memcpy(ls.address, s.address, 8);
        if (s.missing) {
            bits |= (1u << (SENSOR_MISSING_ALARM_BIT_BASE + i));
        }
    }
    for (size_t i = _count; i < MAX_LOGICAL_SENSORS; ++i) {
        state.sensors.sensor[i] = LogicalSensorStatus{};
    }

    state.oneWire.count = static_cast<uint8_t>(_scanDeviceCount);
    state.oneWire.done = _scanDone;
    state.oneWire.overflow = _scanOverflow;
    state.oneWire.scanCount = _scanRunCount;
    for (size_t i = 0; i < _scanDeviceCount; ++i) {
        memcpy(state.oneWire.address[i], _scanAddr[i], 8);
        state.oneWire.logical[i] = _scanLogical[i];
        state.oneWire.tempC[i] = _scanTempC[i];
        state.oneWire.tempValid[i] = _scanTempValid[i];
    }
    for (size_t i = _scanDeviceCount; i < ONE_WIRE_MAX_DEVICES; ++i) {
        memset(state.oneWire.address[i], 0, 8);
        state.oneWire.logical[i] = NO_LOGICAL_SENSOR;
        state.oneWire.tempC[i] = 0.0f;
        state.oneWire.tempValid[i] = false;
    }

    state.alarms.activeMask = (state.alarms.activeMask & ~SENSOR_MISSING_ALARM_MASK) | bits;
}

void SensorService::syncMapping(uint64_t nowMs, EventReason reason) {
    bool changed[MAX_LOGICAL_SENSORS] = {};
    uint8_t newAddress[MAX_LOGICAL_SENSORS][8] = {};
    bool newAssigned[MAX_LOGICAL_SENSORS] = {};

    for (size_t i = 0; i < _count; ++i) {
        Sensor& s = _sensors[i];
        const char* text = _config.getText(static_cast<size_t>(s.settingIndex));

        if (strncmp(text, s.cachedText, sizeof(s.cachedText)) == 0) {
            newAssigned[i] = s.assigned;
            memcpy(newAddress[i], s.address, 8);
            continue;
        }

        changed[i] = true;
        uint8_t addr[8];
        bool valid = (text[0] == '\0') || (parseAddress(text, addr) && Ds18b20::isValidRom(addr));

        if (!valid) {
            _config.setText(static_cast<size_t>(s.settingIndex), "", reason, nowMs);
            s.cachedText[0] = '\0';
            newAssigned[i] = false;
            memset(newAddress[i], 0, 8);
            continue;
        }

        strncpy(s.cachedText, text, sizeof(s.cachedText) - 1);
        s.cachedText[sizeof(s.cachedText) - 1] = '\0';
        if (text[0] == '\0') {
            newAssigned[i] = false;
            memset(newAddress[i], 0, 8);
        } else {
            newAssigned[i] = true;
            memcpy(newAddress[i], addr, 8);
        }
    }

    // Duplicate resolution: the lower logical index keeps the address.
    for (size_t i = 0; i < _count; ++i) {
        if (!newAssigned[i]) {
            continue;
        }
        for (size_t j = i + 1; j < _count; ++j) {
            if (newAssigned[j] && addressEquals(newAddress[i], newAddress[j])) {
                _config.setText(static_cast<size_t>(_sensors[j].settingIndex), "", reason, nowMs);
                _sensors[j].cachedText[0] = '\0';
                newAssigned[j] = false;
                memset(newAddress[j], 0, 8);
                changed[j] = true;
            }
        }
    }

    for (size_t i = 0; i < _count; ++i) {
        Sensor& s = _sensors[i];
        bool addrChanged = changed[i] && (newAssigned[i] != s.assigned || !addressEquals(newAddress[i], s.address));
        s.assigned = newAssigned[i];
        memcpy(s.address, newAddress[i], 8);

        if (addrChanged) {
            s.debounce.reset();
            s.missingScan = false;
            s.goodSinceScan = false;
            if (s.missing || s.alarmRaised) {
                s.missing = false;
                if (s.alarmRaised) {
                    s.alarmRaised = false;
                    _events.logEvent(toU16(EventType::AlarmCleared),
                        static_cast<uint16_t>(EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + i),
                        static_cast<float>(i), 0.0f, EventReason::Logic);
                }
            }
            recomputeMissingScan(i);
        }
    }
    refreshScanLogical();
}

void SensorService::doScan(uint64_t nowMs) {
    (void)nowMs;
    // Search with the raw capacity (ONE_WIRE_SEARCH_MAX > ONE_WIRE_MAX_DEVICES)
    // and keep only valid DS18B20 ROMs (family 0x28 + CRC8, D17): a corrupted
    // ROM or a non-DS18B20 device never appears in the published list, never
    // takes one of the ONE_WIRE_MAX_DEVICES slots and never causes a false
    // overflow. Overflow = more valid devices than slots, or the raw search
    // itself was truncated.
    uint8_t raw[ONE_WIRE_SEARCH_MAX][8];
    bool truncated = false;
    size_t rawCount = _bus.search(raw, ONE_WIRE_SEARCH_MAX, truncated);
    if (rawCount > ONE_WIRE_SEARCH_MAX) {
        rawCount = ONE_WIRE_SEARCH_MAX;
    }
    size_t found = 0;
    bool overflow = truncated;
    for (size_t r = 0; r < rawCount; ++r) {
        if (!Ds18b20::isValidRom(raw[r])) {
            continue;
        }
        if (found >= ONE_WIRE_MAX_DEVICES) {
            overflow = true;
            continue;
        }
        memcpy(_scanAddr[found], raw[r], 8);
        ++found;
    }
    _scanDeviceCount = found;
    _scanOverflow = overflow;
    _scanDone = true;
    ++_scanRunCount;

    for (size_t i = 0; i < found; ++i) {
        _scanTempValid[i] = false;
        _scanTempC[i] = 0.0f;
        _scanLogical[i] = NO_LOGICAL_SENSOR;
    }

    for (size_t li = 0; li < _count; ++li) {
        _sensors[li].goodSinceScan = false;
        recomputeMissingScan(li);
    }
    refreshScanLogical();

    if (overflow) {
        _events.logEvent(toU16(EventType::DiagnosticWarning),
            static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ONEWIRE_OVERFLOW), 0.0f, 0.0f, EventReason::Logic);
    }
}

void SensorService::recomputeMissingScan(size_t logical) {
    Sensor& s = _sensors[logical];
    if (!s.assigned) {
        s.missingScan = false;
        return;
    }
    bool found = false;
    for (size_t k = 0; k < _scanDeviceCount; ++k) {
        if (addressEquals(_scanAddr[k], s.address)) {
            found = true;
            break;
        }
    }
    s.missingScan = !found;
}

void SensorService::refreshScanLogical() {
    for (size_t i = 0; i < _scanDeviceCount; ++i) {
        _scanLogical[i] = NO_LOGICAL_SENSOR;
        for (size_t li = 0; li < _count; ++li) {
            if (_sensors[li].assigned && addressEquals(_sensors[li].address, _scanAddr[i])) {
                _scanLogical[i] = static_cast<uint8_t>(li);
                break;
            }
        }
    }
}

void SensorService::addUniqueAddress(uint8_t addrs[][8], size_t& n, const uint8_t addr[8]) const {
    for (size_t i = 0; i < n; ++i) {
        if (addressEquals(addrs[i], addr)) {
            return;
        }
    }
    if (n < MAX_READ_SET) {
        memcpy(addrs[n], addr, 8);
        ++n;
    }
}

void SensorService::busCycle(uint64_t nowMs) {
    if (!_pendingRead) {
        if (!_everRequested || nowMs - _lastRequestMs >= CYCLE_MS) {
            _bus.requestConversionAll();
            _requestMs = nowMs;
            _lastRequestMs = nowMs;
            _everRequested = true;
            _pendingRead = true;
        }
        return;
    }

    if (nowMs - _requestMs < CONVERSION_MS) {
        return;
    }

    uint8_t readAddr[MAX_READ_SET][8];
    size_t readCount = 0;
    for (size_t i = 0; i < _scanDeviceCount; ++i) {
        addUniqueAddress(readAddr, readCount, _scanAddr[i]);
    }
    for (size_t i = 0; i < _count; ++i) {
        if (_sensors[i].assigned) {
            addUniqueAddress(readAddr, readCount, _sensors[i].address);
        }
    }

    for (size_t r = 0; r < readCount; ++r) {
        uint8_t scratch[9];
        bool presence = _bus.readScratchpad(readAddr[r], scratch);
        SensorReading reading = Ds18b20::decodeScratchpad(presence, scratch);

        for (size_t i = 0; i < _scanDeviceCount; ++i) {
            if (addressEquals(_scanAddr[i], readAddr[r])) {
                _scanTempValid[i] = (reading.status == ReadStatus::Ok);
                _scanTempC[i] = reading.tempC;
                break;
            }
        }

        for (size_t li = 0; li < _count; ++li) {
            Sensor& s = _sensors[li];
            if (s.assigned && addressEquals(s.address, readAddr[r])) {
                if (reading.status == ReadStatus::Ok) {
                    s.goodSinceScan = true;
                }
                SensorDebounce::Transition t = s.debounce.onReading(reading);
                if (t == SensorDebounce::Transition::Faulted) {
                    _events.logEvent(toU16(EventType::SensorFault), static_cast<uint16_t>(EVENT_SOURCE_SENSOR_BASE + li),
                        static_cast<float>(static_cast<uint8_t>(s.debounce.lastBadStatus())), 0.0f, EventReason::Logic);
                } else if (t == SensorDebounce::Transition::Recovered) {
                    std::optional<float> v = s.debounce.value();
                    _events.logEvent(toU16(EventType::SensorRecovered), static_cast<uint16_t>(EVENT_SOURCE_SENSOR_BASE + li),
                        v.has_value() ? *v : 0.0f, 0.0f, EventReason::Logic);
                }
                break;
            }
        }
    }

    _pendingRead = false;
}

void SensorService::evaluateMissing(uint64_t nowMs) {
    (void)nowMs;
    for (size_t li = 0; li < _count; ++li) {
        Sensor& s = _sensors[li];
        if (!s.assigned) {
            if (s.missing || s.alarmRaised) {
                s.missing = false;
                s.alarmRaised = false;
                _events.logEvent(toU16(EventType::AlarmCleared),
                    static_cast<uint16_t>(EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + li),
                    static_cast<float>(li), 0.0f, EventReason::Logic);
            }
            continue;
        }

        bool faultNoResponse =
            (s.debounce.state() == SensorState::Fault && s.debounce.lastBadStatus() == ReadStatus::NoResponse);
        bool newMissing = (s.missingScan && !s.goodSinceScan) || faultNoResponse;

        if (newMissing != s.missing) {
            s.missing = newMissing;
            s.alarmRaised = newMissing;
            _events.logEvent(newMissing ? toU16(EventType::AlarmRaised) : toU16(EventType::AlarmCleared),
                static_cast<uint16_t>(EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + li),
                static_cast<float>(li), 0.0f, EventReason::Logic);
        }
    }
}
