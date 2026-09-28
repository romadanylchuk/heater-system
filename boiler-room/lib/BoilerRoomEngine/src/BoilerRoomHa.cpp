#include "BoilerRoomHa.h"
#include <stdio.h>
#include "BoilerRoomAlarms.h"
#include "BoilerRoomDiagnostics.h"

namespace {

const BoilerRoomStatus* g_status = nullptr;

const BoilerRoomStatus* readyStatus() {
    return (g_status != nullptr && g_status->ready) ? g_status : nullptr;
}

bool writeOnOff(bool on, char* out, size_t cap) {
    snprintf(out, cap, "%s", on ? "ON" : "OFF");
    return true;
}

bool energyState(const CommonState&, char* out, size_t cap) {
    const BoilerRoomStatus* st = readyStatus();
    if (st == nullptr || st->energyQuality == EnergyQuality::Unavailable) {
        return false;
    }
    snprintf(out, cap, "%.1f", static_cast<double>(st->energyKWh));
    return true;
}

bool energyEstimatedState(const CommonState&, char* out, size_t cap) {
    const BoilerRoomStatus* st = readyStatus();
    if (st == nullptr || st->energyQuality == EnergyQuality::Unavailable) {
        return false;
    }
    return writeOnOff(st->energyQuality == EnergyQuality::Estimated, out, cap);
}

bool p3ModeState(const CommonState&, char* out, size_t cap) {
    const BoilerRoomStatus* st = readyStatus();
    if (st == nullptr) {
        return false;
    }
    snprintf(out, cap, "%s", p3ModeKey(st->p3Mode));
    return true;
}

bool antiFreezeRunState(const CommonState&, char* out, size_t cap) {
    const BoilerRoomStatus* st = readyStatus();
    if (st == nullptr) {
        return false;
    }
    return writeOnOff(st->antiFreezeRunning, out, cap);
}

template <uint8_t BIT>
bool alarmState(const CommonState& s, char* out, size_t cap) {
    static_assert(BIT < 32, "alarm bit out of range");
    return writeOnOff(((s.alarms.activeMask >> BIT) & 1u) != 0, out, cap);
}

// Stage 09 (C9): a pump-response warning bit of CommonState.diag.warningMask.
// Always available, like the alarm entities.
template <uint8_t BIT>
bool warnState(const CommonState& s, char* out, size_t cap) {
    static_assert(BIT < 8, "warning bit outside the project-owned range (D6)");
    return writeOnOff(((s.diag.warningMask >> BIT) & 1u) != 0, out, cap);
}

constexpr const char* PROBLEM = "problem";
constexpr const char* DIAG = "diagnostic";
constexpr uint8_t S = BR_ALARM_SENSOR_BASE;

}  // namespace

void bindBoilerRoomHaStatus(const BoilerRoomStatus* status) {
    g_status = status;
}

const HaCustomEntity BOILER_ROOM_HA_ENTITIES[BOILER_ROOM_HA_ENTITY_TOTAL] = {
    {"energy", "Stored energy", HaComponent::Sensor, "kWh", "energy_storage", "measurement", nullptr, energyState},
    {"energy_estimated", "Stored energy estimated", HaComponent::BinarySensor, nullptr, nullptr, nullptr, DIAG,
        energyEstimatedState},
    {"p3_mode", "P3 mode", HaComponent::Sensor, nullptr, nullptr, nullptr, nullptr, p3ModeState},
    {"anti_freeze_run", "Anti-freeze run", HaComponent::BinarySensor, nullptr, "running", nullptr, nullptr,
        antiFreezeRunState},
    {"alarm_overheat", "Alarm overheat", HaComponent::BinarySensor, nullptr, "heat", nullptr, nullptr,
        alarmState<BR_ALARM_OVERHEAT>},
    {"alarm_t1_failsafe", "Alarm T1 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<BR_ALARM_T1_FAILSAFE>},
    {"alarm_t1t2_failsafe", "Alarm T1+T2 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<BR_ALARM_T1T2_FAILSAFE>},
    {"alarm_t2_failsafe", "Alarm T2 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<BR_ALARM_T2_FAILSAFE>},
    {"alarm_t3_failsafe", "Alarm T3 fail-safe", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        alarmState<BR_ALARM_T3_FAILSAFE>},
    {"alarm_t1_fault", "Alarm T1 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 0>},
    {"alarm_t2_fault", "Alarm T2 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 1>},
    {"alarm_t3_fault", "Alarm T3 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 2>},
    {"alarm_t4_fault", "Alarm T4 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 3>},
    {"alarm_t5_fault", "Alarm T5 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 4>},
    {"alarm_t6_fault", "Alarm T6 fault", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, DIAG,
        alarmState<S + 5>},
    {"warn_p3_no_flow", "P3 no flow (B1)", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        warnState<BR_WARN_B1>},
    {"warn_p1_not_charging", "P1 not charging (B3)", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        warnState<BR_WARN_B3>},
    {"warn_p2_no_effect", "P2 no effect (B6)", HaComponent::BinarySensor, nullptr, PROBLEM, nullptr, nullptr,
        warnState<BR_WARN_B6>},
};
const size_t BOILER_ROOM_HA_ENTITY_COUNT = sizeof(BOILER_ROOM_HA_ENTITIES) / sizeof(BOILER_ROOM_HA_ENTITIES[0]);
