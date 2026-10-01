#include "HomeHeatingPages.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <DisplayFormat.h>
#include "HomeHeatingAlarms.h"

namespace {

constexpr size_t ROW_BUF = 32;
constexpr size_t TEMP_BUF = 12;
constexpr float PERCENT_MAX = 100.0f;
constexpr float SETPOINT_DISPLAY_MAX = 999.9f;   // keeps "H2 -55.0 set -999.9" (19) within 21 chars
constexpr const char* NO_TEMP = "--.-";

const HomeHeatingStatus* readyStatus(void* ctx) {
    const HomeHeatingStatus* st = static_cast<const HomeHeatingStatus*>(ctx);
    return (st != nullptr && st->ready) ? st : nullptr;
}

// Title plus "starting..." when there is no ready status; true when rendered.
bool renderStarting(const HomeHeatingStatus* st, DisplayFrame& f) {
    if (st != nullptr) {
        return false;
    }
    f.addBody(0, "starting...");
    return true;
}

// "45.2" / "-3.5"; "--.-" unless the logical sensor is Ok with a displayable value.
void sensorTemp(const CommonState& s, uint8_t sensor, char* out, size_t cap) {
    const bool ok = sensor < s.sensors.count && s.sensors.sensor[sensor].state == SensorState::Ok;
    formatTempC(ok ? s.sensors.sensor[sensor].tempC : 0.0f, ok, out, cap);
    if (strcmp(out, "--") == 0) {
        snprintf(out, cap, "%s", NO_TEMP);
    }
}

void tempRow(const CommonState& s, uint8_t sensor, const char* label, char* out, size_t cap) {
    char t[TEMP_BUF];
    sensorTemp(s, sensor, t, sizeof(t));
    snprintf(out, cap, "%s %s", label, t);
}

// Clamped into [-999.9, 999.9] (NaN -> 0) so the row never overflows.
double displaySetpoint(float c) {
    if (isnan(c)) {
        return 0.0;
    }
    if (c < -SETPOINT_DISPLAY_MAX) {
        return static_cast<double>(-SETPOINT_DISPLAY_MAX);
    }
    return static_cast<double>(c > SETPOINT_DISPLAY_MAX ? SETPOINT_DISPLAY_MAX : c);
}

bool relayOn(const CommonState& s, uint8_t ch) {
    return ch < RELAY_CHANNEL_COUNT && s.relays.on[ch];
}

const char* motionMark(int8_t moving) {
    return moving > 0 ? " >" : (moving < 0 ? " <" : "");
}

// "K1 35% >" (known), "K1 recal" (recalibrating), "K1 ?" (unknown).
void k1Row(const HomeHeatingStatus& st, char* out, size_t cap) {
    const char* mark = motionMark(st.k1Moving);
    if (st.k1Mode == K1Mode::Recalibrating) {
        snprintf(out, cap, "K1 recal%s", mark);
    } else if (!st.k1Known) {
        snprintf(out, cap, "K1 ?%s", mark);
    } else {
        float pct = st.k1PosPct;
        if (!(pct >= 0.0f)) {
            pct = 0.0f;
        }
        pct = pct > PERCENT_MAX ? PERCENT_MAX : pct;
        snprintf(out, cap, "K1 %ld%%%s", lroundf(pct), mark);
    }
}

// The fail-safe row: the matching alarm label (C8 texts, all <= 21 chars).
const char* failText(FailMode fail) {
    uint8_t bit = HOME_HEATING_ALARM_COUNT;
    switch (fail) {
        case FailMode::H3: bit = HH_ALARM_H3_FAILSAFE; break;
        case FailMode::H2: bit = HH_ALARM_H2_FAILSAFE; break;
        case FailMode::H1: bit = HH_ALARM_H1_FAILSAFE; break;
        case FailMode::Multi: bit = HH_ALARM_MULTI_FAILSAFE; break;
        case FailMode::None:
        default: return nullptr;
    }
    return HOME_HEATING_ALARMS[bit].labelEn;
}

}  // namespace

void renderHeatingPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    const HomeHeatingStatus* st = readyStatus(ctx);
    f.clear();
    f.setTitle("Heating");
    if (renderStarting(st, f)) {
        return;
    }
    char row[ROW_BUF];
    char t[TEMP_BUF];
    sensorTemp(s, HH_SENSOR_H2, t, sizeof(t));
    snprintf(row, sizeof(row), "H2 %s set %.1f", t, displaySetpoint(st->h2Set));
    f.addBody(0, row);
    tempRow(s, HH_SENSOR_H3, "H3", row, sizeof(row));
    f.addBody(1, row);
    k1Row(*st, row, sizeof(row));
    f.addBody(2, row);
    snprintf(row, sizeof(row), "P4 %s %s", relayOn(s, HH_RELAY_P4) ? "ON" : "OFF", p4ReasonShort(st->p4Reason));
    f.addBody(3, row);
    const char* fail = failText(st->fail);
    if (fail != nullptr) {
        f.addBody(4, fail);
    }
}

void renderDhwPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    const HomeHeatingStatus* st = readyStatus(ctx);
    f.clear();
    f.setTitle("DHW");
    if (renderStarting(st, f)) {
        return;
    }
    char row[ROW_BUF];
    tempRow(s, HH_SENSOR_H3, "H3", row, sizeof(row));
    f.addBody(0, row);
    tempRow(s, HH_SENSOR_H4, "H4", row, sizeof(row));
    f.addBody(1, row);
    snprintf(row, sizeof(row), "K2 %s %s", hhK2BypassFromRelay(relayOn(s, HH_RELAY_K2)) ? "BYP" : "TANK", k2ReasonShort(st->k2Reason));
    f.addBody(2, row);
    snprintf(row, sizeof(row), "No need: %s", st->noNeed ? "yes" : "no");
    f.addBody(3, row);
}
