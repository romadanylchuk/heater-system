#pragma once
#include <stdint.h>
#include <optional>
#include <HardwareStatus.h>
#include "Ds18b20Codec.h"

// Per-logical-sensor debounce (D14/D15): 3 consecutive good/bad readings to
// cross Unknown/Ok/Fault, plus the DS18B20 85 degC power-on-sentinel rule
// (D14). Pure, no time source -- driven once per bus read (SensorService,
// ~2 s cadence).
class SensorDebounce {
public:
    static constexpr uint8_t FAULT_COUNT = 3;
    static constexpr uint8_t RECOVER_COUNT = 3;
    static constexpr float POWER_ON_TEMP_C = 85.0f;
    static constexpr float POWER_ON_JUMP_C = 5.0f;

    enum class Transition : uint8_t { None, BecameOk, Faulted, Recovered };

    // -> Unknown, counters 0, no history. Called on boot and on (re)assignment.
    void reset();

    // Applies the 85 degC power-on rule (D14), then debounces (D15).
    Transition onReading(const SensorReading& reading);

    SensorState state() const { return _state; }
    std::optional<float> value() const;  // last good reading, only when state()==Ok
    ReadStatus lastBadStatus() const { return _lastBad; }
    bool lastSampleGood() const { return !_prevSampleBad; }

private:
    static uint8_t incSaturating(uint8_t v) { return v < 255 ? static_cast<uint8_t>(v + 1) : uint8_t{255}; }

    SensorState _state = SensorState::Unknown;
    uint8_t _goodRun = 0;
    uint8_t _badRun = 0;
    bool _hasGood = false;
    float _lastGood = 0.0f;
    bool _prevSampleBad = true;
    ReadStatus _lastBad = ReadStatus::NoResponse;
};
