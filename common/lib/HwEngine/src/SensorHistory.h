#pragma once
#include <stdint.h>

// Fixed-size per-sensor temperature history (stage 09, C1): one sample of
// every logical sensor every SENSOR_HISTORY_PERIOD_MS, SENSOR_HISTORY_DEPTH
// samples deep (60 min). Used by the diagnostics rules and the K1 step test
// to answer "has this sensor been steady for N samples?". Pure, no heap:
// time is injected via tick(nowMs). Missing readings are stored as NAN.
constexpr uint8_t  SENSOR_HISTORY_MAX_SENSORS = 8;      // == MAX_LOGICAL_SENSORS
constexpr uint32_t SENSOR_HISTORY_PERIOD_MS   = 30000;  // one sample every 30 s
constexpr uint16_t SENSOR_HISTORY_DEPTH       = 120;    // 60 min

class SensorHistory {
public:
    // Clamps sensorCount to SENSOR_HISTORY_MAX_SENSORS, fills every slot with
    // NAN and empties the history. The first tick() afterwards samples
    // immediately.
    void reset(uint8_t sensorCount);

    // Samples every sensor when due. The first call after reset() is due.
    // Afterwards the next due time = previous due + PERIOD. If now >= previous
    // due + PERIOD (a stalled loop), the next due time = now + PERIOD: exactly
    // one sample, no catch-up burst. values[i] is stored when ok[i] is true,
    // else NAN. Returns true when a sample was taken.
    bool tick(uint64_t nowMs, const float values[], const bool ok[]);

    uint8_t sensorCount() const { return _count; }
    uint16_t size() const { return _size; }  // samples held, 0..DEPTH (same for all sensors)

    // age 0 = newest; NAN when sensor or age is out of range.
    float at(uint8_t sensor, uint16_t age) const;

    // size() >= samples AND each of the newest `samples` values of `sensor`
    // is finite with |v - ref| <= band. samples == 0 or > DEPTH -> false.
    bool steady(uint8_t sensor, float ref, float band, uint16_t samples) const;

private:
    float _v[SENSOR_HISTORY_MAX_SENSORS][SENSOR_HISTORY_DEPTH];
    uint16_t _head = 0;  // index of the next write slot
    uint16_t _size = 0;
    uint8_t _count = 0;
    bool _started = false;
    uint64_t _dueMs = 0;
};
