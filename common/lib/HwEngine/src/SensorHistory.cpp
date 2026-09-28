#include "SensorHistory.h"
#include <math.h>
#include <HardwareStatus.h>

static_assert(SENSOR_HISTORY_MAX_SENSORS == MAX_LOGICAL_SENSORS,
              "SensorHistory must hold every logical sensor");

void SensorHistory::reset(uint8_t sensorCount) {
    _count = sensorCount > SENSOR_HISTORY_MAX_SENSORS ? SENSOR_HISTORY_MAX_SENSORS : sensorCount;
    for (uint8_t s = 0; s < SENSOR_HISTORY_MAX_SENSORS; ++s) {
        for (uint16_t i = 0; i < SENSOR_HISTORY_DEPTH; ++i) {
            _v[s][i] = NAN;
        }
    }
    _head = 0;
    _size = 0;
    _started = false;
    _dueMs = 0;
}

bool SensorHistory::tick(uint64_t nowMs, const float values[], const bool ok[]) {
    if (_started) {
        if (nowMs < _dueMs) {
            return false;
        }
        // Drift-free cadence; a stall of a full period or more restarts the
        // grid from now so there is never a catch-up burst.
        if (nowMs >= _dueMs + SENSOR_HISTORY_PERIOD_MS) {
            _dueMs = nowMs + SENSOR_HISTORY_PERIOD_MS;
        } else {
            _dueMs += SENSOR_HISTORY_PERIOD_MS;
        }
    } else {
        _started = true;
        _dueMs = nowMs + SENSOR_HISTORY_PERIOD_MS;
    }

    for (uint8_t s = 0; s < _count; ++s) {
        _v[s][_head] = ok[s] ? values[s] : NAN;
    }
    _head = static_cast<uint16_t>((_head + 1) % SENSOR_HISTORY_DEPTH);
    if (_size < SENSOR_HISTORY_DEPTH) {
        ++_size;
    }
    return true;
}

float SensorHistory::at(uint8_t sensor, uint16_t age) const {
    if (sensor >= _count || age >= _size) {
        return NAN;
    }
    const uint16_t idx = static_cast<uint16_t>((_head + SENSOR_HISTORY_DEPTH - 1 - age) % SENSOR_HISTORY_DEPTH);
    return _v[sensor][idx];
}

bool SensorHistory::steady(uint8_t sensor, float ref, float band, uint16_t samples) const {
    if (samples == 0 || samples > SENSOR_HISTORY_DEPTH || sensor >= _count || _size < samples) {
        return false;
    }
    for (uint16_t age = 0; age < samples; ++age) {
        const float v = at(sensor, age);
        if (!isfinite(v) || fabsf(v - ref) > band) {
            return false;
        }
    }
    return true;
}
