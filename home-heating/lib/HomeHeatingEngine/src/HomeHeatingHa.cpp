#include "HomeHeatingHa.h"
#include <math.h>
#include <stdio.h>
#include "HomeHeatingAlarms.h"
#include "HomeHeatingDiagnostics.h"

namespace {

const HomeHeatingStatus* g_status = nullptr;
constexpr float PERCENT_MAX = 100.0f;

const HomeHeatingStatus* readyStatus() {
    return (g_status != nullptr && g_status->ready) ? g_status : nullptr;
}

bool writeOnOff(bool on, char* out, size_t cap) {
    snprintf(out, cap, "%s", on ? "ON" : "OFF");
    return true;
}

bool writeText(const char* text, char* out, size_t cap) {
    snprintf(out, cap, "%s", text);
    return true;
}

bool noNeedState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && writeOnOff(st->noNeed, out, cap);
}

bool p4ReasonState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && writeText(p4ReasonKey(st->p4Reason), out, cap);
}

bool k2ModeState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && writeText(st->k2Bypass ? "bypass" : "tank", out, cap);
}

bool k2ReasonState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && writeText(k2ReasonKey(st->k2Reason), out, cap);
}

bool k1PositionState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    if (st == nullptr || !st->k1Known) {
        return false;
    }
    float pct = st->k1PosPct;
    if (!(pct >= 0.0f)) {
        pct = 0.0f;
    }
    pct = pct > PERCENT_MAX ? PERCENT_MAX : pct;
    snprintf(out, cap, "%ld", lroundf(pct));
    return true;
}

bool k1ModeState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && writeText(k1ModeKey(st->k1Mode), out, cap);
}

bool failsafeState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && writeText(failModeKey(st->fail), out, cap);
}

template <uint8_t BIT>
bool alarmState(const CommonState& s, char* out, size_t cap) {
    static_assert(BIT < 32, "alarm bit out of range");
    return writeOnOff(((s.alarms.activeMask >> BIT) & 1u) != 0, out, cap);
}

// ---- stage 09 (C15) ----

template <uint8_t BIT>
bool warnState(const CommonState& s, char* out, size_t cap) {
    static_assert(BIT < 32, "warning bit out of range");
    return writeOnOff(((s.diag.warningMask >> BIT) & 1u) != 0, out, cap);
}

bool writeOneDecimal(float v, char* out, size_t cap) {
    if (!isfinite(v)) {
        return false;
    }
    snprintf(out, cap, "%.1f", static_cast<double>(v));
    return true;
}

bool h2ErrorState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && st->h2ErrValid && writeOneDecimal(st->h2ErrC, out, cap);
}

bool k1LastPulseState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    return st != nullptr && st->lastPulseDir != 0 && writeOneDecimal(st->lastPulseS, out, cap);
}

bool k1LastPulseDirState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    if (st == nullptr) {
        return false;
    }
    return writeText(st->lastPulseDir > 0 ? "open" : (st->lastPulseDir < 0 ? "close" : "none"), out, cap);
}

bool k1PulsesTodayState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    if (st == nullptr) {
        return false;
    }
    snprintf(out, cap, "%lu", static_cast<unsigned long>(st->pulsesToday));
    return true;
}

bool k1PulsesYesterdayState(const CommonState&, char* out, size_t cap) {
    const HomeHeatingStatus* st = readyStatus();
    if (st == nullptr || !st->pulsesYesterdayValid) {
        return false;
    }
    snprintf(out, cap, "%lu", static_cast<unsigned long>(st->pulsesYesterday));
    return true;
}

constexpr const char* PROBLEM = "problem";
constexpr const char* DIAG = "diagnostic";
constexpr uint8_t S = HH_ALARM_SENSOR_BASE;
constexpr const char* DEG_C = "\xC2\xB0" "C";   // degC in UTF-8, as HaDiscovery's temperature unit

}  // namespace

void bindHomeHeatingHaStatus(const HomeHeatingStatus* status) {
    g_status = status;
}

const HaCustomEntity HOME_HEATING_HA_ENTITIES[HOME_HEATING_HA_ENTITY_TOTAL] = {
    {"no_need", "No need", HaComponent::BinarySensor, nullptr, nullptr, nullptr, nullptr, noNeedState},
    {"p4_reason", "P4 reason", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, p4ReasonState},
    {"k2_mode", "K2 mode", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, k2ModeState},
    {"k2_reason", "K2 reason", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, k2ReasonState},
    {"k1_position", "K1 position", HaComponent::Sensor, "%", nullptr, "measurement", nullptr, k1PositionState},
    {"k1_mode", "K1 mode", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, k1ModeState},
    {"failsafe", "Fail-safe mode", HaComponent::Sensor, nullptr, nullptr, nullptr, DIAG, failsafeState},
    {"alarm_h3_failsafe", "Alarm H3 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<HH_ALARM_H3_FAILSAFE>},
    {"alarm_h2_failsafe", "Alarm H2 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<HH_ALARM_H2_FAILSAFE>},
    {"alarm_h1_failsafe", "Alarm H1 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<HH_ALARM_H1_FAILSAFE>},
    {"alarm_multi_failsafe", "Alarm sensors fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr,
        nullptr, alarmState<HH_ALARM_MULTI_FAILSAFE>},
    {"alarm_h4_failsafe", "Alarm H4 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<HH_ALARM_H4_FAILSAFE>},
    {"alarm_h1_fault", "Alarm H1 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 0>},
    {"alarm_h2_fault", "Alarm H2 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 1>},
    {"alarm_h3_fault", "Alarm H3 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 2>},
    {"alarm_h4_fault", "Alarm H4 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 3>},
    // ---- stage 09 (C15), appended ----
    {"warn_p4_no_flow", "P4 no flow (H1)", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        warnState<HH_WARN_H1>},
    {"h2_error", "H2 error", HaComponent::Sensor, DEG_C, nullptr, "measurement", nullptr, h2ErrorState},
    {"k1_last_pulse", "K1 last pulse", HaComponent::Sensor, "s", nullptr, "measurement", nullptr, k1LastPulseState},
    {"k1_last_pulse_dir", "K1 last pulse direction", HaComponent::Sensor, nullptr, nullptr, nullptr, DIAG,
        k1LastPulseDirState},
    {"k1_pulses_today", "K1 pulses today", HaComponent::Sensor, nullptr, nullptr, "total_increasing", DIAG,
        k1PulsesTodayState},
    {"k1_pulses_yesterday", "K1 pulses yesterday", HaComponent::Sensor, nullptr, nullptr, nullptr, DIAG,
        k1PulsesYesterdayState},
};
const size_t HOME_HEATING_HA_ENTITY_COUNT = sizeof(HOME_HEATING_HA_ENTITIES) / sizeof(HOME_HEATING_HA_ENTITIES[0]);
