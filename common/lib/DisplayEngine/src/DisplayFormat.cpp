#include "DisplayFormat.h"
#include <math.h>
#include <stdio.h>

static constexpr uint8_t SENSOR_ALARM_FIRST_BIT = 24;
static constexpr float DS18B20_MIN_C = -55.0f;
static constexpr float DS18B20_MAX_C = 125.0f;

const char* wifiStateText(const NetworkStatus& n) {
    if (n.wifiConnected) {
        return "connected";
    }
    if (n.setupApActive) {
        return "setup AP";
    }
    if (n.wifiMode == NetWifiMode::Station) {
        return "connecting";
    }
    return "disconnected";
}

const char* mqttStateText(const NetworkStatus& n) {
    if (!n.mqttEnabled) {
        return "disabled";
    }
    return n.mqttConnected ? "connected" : "disconnected";
}

bool formatRssi(const NetworkStatus& n, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return false;
    }
    out[0] = '\0';
    if (!n.wifiConnected) {
        return false;
    }
    snprintf(out, cap, "%d dBm", static_cast<int>(n.wifiRssi));
    return true;
}

void formatTempC(float c, bool valid, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    if (!valid || !isfinite(c) || c < DS18B20_MIN_C || c > DS18B20_MAX_C) {
        snprintf(out, cap, "--");
        return;
    }
    snprintf(out, cap, "%.1f", static_cast<double>(c));
}

const char* sensorStateText(const LogicalSensorStatus& s) {
    if (!s.assigned) {
        return "--";
    }
    if (s.missing) {
        return "MISS";
    }
    if (s.state == SensorState::Fault) {
        return "FAULT";
    }
    if (s.state == SensorState::Ok) {
        return "OK";
    }
    return "WAIT";
}

void alarmLabel(uint8_t bit, const DisplayLabels& labels, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    if (bit >= SENSOR_ALARM_FIRST_BIT && labels.sensors != nullptr &&
            static_cast<size_t>(bit - SENSOR_ALARM_FIRST_BIT) < labels.sensorCount) {
        const char* name = labels.sensors[bit - SENSOR_ALARM_FIRST_BIT].name;
        snprintf(out, cap, "Sensor %s missing", name != nullptr ? name : "?");
        return;
    }
    if (labels.alarms != nullptr && bit < labels.alarmCount && labels.alarms[bit].labelEn != nullptr) {
        snprintf(out, cap, "%s", labels.alarms[bit].labelEn);
        return;
    }
    snprintf(out, cap, "Alarm %u", static_cast<unsigned>(bit));
}

uint8_t alarmCount(uint32_t mask) {
    uint8_t count = 0;
    while (mask != 0) {
        mask &= mask - 1;
        ++count;
    }
    return count;
}
