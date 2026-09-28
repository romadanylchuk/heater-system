#include "BoilerRoomPages.h"
#include <stdio.h>
#include <DisplayFormat.h>

namespace {

constexpr size_t ROW_BUF = 32;
constexpr size_t TEMP_BUF = 12;
constexpr uint32_t SECONDS_PER_MIN = 60;
constexpr float ENERGY_DISPLAY_MAX = 99999.9f;   // keeps "E ~99999.9 kWh est" (18) within 21 chars

// Clamped into [0, ENERGY_DISPLAY_MAX] (non-finite -> 0) so the row never overflows.
double displayEnergy(float kWh) {
    if (!(kWh >= 0.0f)) {
        return 0.0;
    }
    return static_cast<double>(kWh > ENERGY_DISPLAY_MAX ? ENERGY_DISPLAY_MAX : kWh);
}

const BoilerRoomStatus* readyStatus(void* ctx) {
    const BoilerRoomStatus* st = static_cast<const BoilerRoomStatus*>(ctx);
    return (st != nullptr && st->ready) ? st : nullptr;
}

// Title plus "starting..." when there is no ready status; true when rendered.
bool renderStarting(const BoilerRoomStatus* st, DisplayFrame& f) {
    if (st != nullptr) {
        return false;
    }
    f.addBody(0, "starting...");
    return true;
}

// "<label><temp>", "--" unless the logical sensor is Ok.
void tempRow(const CommonState& s, uint8_t sensor, const char* label, char* out, size_t cap) {
    char t[TEMP_BUF];
    const bool ok = sensor < s.sensors.count && s.sensors.sensor[sensor].state == SensorState::Ok;
    formatTempC(ok ? s.sensors.sensor[sensor].tempC : 0.0f, ok, t, sizeof(t));
    snprintf(out, cap, "%s%s", label, t);
}

// "P<n> ON|OFF <reason>[!]": actual relay, controller reason, '!' = safety slot.
void pumpRow(const CommonState& s, const BoilerRoomStatus& st, uint8_t pump, char* out, size_t cap) {
    const BoilerRoomPumpStatus& p = st.pump[pump];
    snprintf(out, cap, "P%u %s %s%s", static_cast<unsigned>(pump + 1), s.relays.on[pump] ? "ON" : "OFF",
        pumpReasonShort(p.reason), p.safety ? "!" : "");
}

// Whole minutes, rounded up so a running countdown never shows 0 (no overflow).
uint32_t minutesCeil(uint32_t seconds) {
    return seconds / SECONDS_PER_MIN + ((seconds % SECONDS_PER_MIN) != 0 ? 1u : 0u);
}

}  // namespace

void renderBoilerPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    const BoilerRoomStatus* st = readyStatus(ctx);
    f.clear();
    f.setTitle("Boiler");
    if (renderStarting(st, f)) {
        return;
    }
    char row[ROW_BUF];
    tempRow(s, BR_SENSOR_T1, "T1 flow   ", row, sizeof(row));
    f.addBody(0, row);
    tempRow(s, BR_SENSOR_T2, "T2 return ", row, sizeof(row));
    f.addBody(1, row);
    pumpRow(s, *st, BR_PUMP_P1, row, sizeof(row));
    f.addBody(2, row);
    pumpRow(s, *st, BR_PUMP_P2, row, sizeof(row));
    f.addBody(3, row);
    if (st->overheat) {
        f.addBody(4, "OVERHEAT");
    }
}

void renderAccuPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    const BoilerRoomStatus* st = readyStatus(ctx);
    f.clear();
    f.setTitle("Accumulator");
    if (renderStarting(st, f)) {
        return;
    }
    char row[ROW_BUF];
    tempRow(s, BR_SENSOR_T3, "T3 top    ", row, sizeof(row));
    f.addBody(0, row);
    tempRow(s, BR_SENSOR_T4, "T4 mid    ", row, sizeof(row));
    f.addBody(1, row);
    tempRow(s, BR_SENSOR_T5, "T5 bottom ", row, sizeof(row));
    f.addBody(2, row);
    switch (st->energyQuality) {
        case EnergyQuality::Exact:
            snprintf(row, sizeof(row), "E %.1f kWh", displayEnergy(st->energyKWh));
            break;
        case EnergyQuality::Estimated:
            snprintf(row, sizeof(row), "E ~%.1f kWh est", displayEnergy(st->energyKWh));
            break;
        case EnergyQuality::Unavailable:
        default:
            snprintf(row, sizeof(row), "E n/a");
            break;
    }
    f.addBody(3, row);
}

void renderSupplyPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    const BoilerRoomStatus* st = readyStatus(ctx);
    f.clear();
    f.setTitle("Supply P3");
    if (renderStarting(st, f)) {
        return;
    }
    char row[ROW_BUF];
    tempRow(s, BR_SENSOR_T6, "T6 supply ", row, sizeof(row));
    f.addBody(0, row);
    pumpRow(s, *st, BR_PUMP_P3, row, sizeof(row));
    f.addBody(1, row);

    // Offer: window minutes left; Off/Normal: wait minutes left (0 = hidden).
    const uint32_t leftS = st->p3Mode == P3Mode::Offer ? st->offerWindowLeftS : st->offerWaitLeftS;
    if (leftS > 0) {
        snprintf(row, sizeof(row), "Mode %s %lum", p3ModeShort(st->p3Mode),
            static_cast<unsigned long>(minutesCeil(leftS)));
    } else {
        snprintf(row, sizeof(row), "Mode %s", p3ModeShort(st->p3Mode));
    }
    f.addBody(2, row);

    if (st->antiFreezeRunning) {
        snprintf(row, sizeof(row), "AF RUN");
    } else if (st->afInS > 0) {
        snprintf(row, sizeof(row), "AF in %lum", static_cast<unsigned long>(minutesCeil(st->afInS)));
    } else {
        snprintf(row, sizeof(row), "AF off");
    }
    f.addBody(3, row);

    // ign = saved but the HA link is down (the gate ignores it, control runs NORMAL).
    const char* flag = !st->noNeedSaved ? "clear" : (st->linkUp ? "set" : "ign");
    snprintf(row, sizeof(row), "No-need %s", flag);
    f.addBody(4, row);
}
