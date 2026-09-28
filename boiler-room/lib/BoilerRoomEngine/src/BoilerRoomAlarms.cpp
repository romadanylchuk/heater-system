#include "BoilerRoomAlarms.h"

const AlarmDescriptor BOILER_ROOM_ALARMS[BOILER_ROOM_ALARM_COUNT] = {
    {"overheat", "Boiler overheat", "Перегрів котла"},
    {"t1_failsafe", "T1 fail: P1 forced", "Збій T1: P1 примусово"},
    {"t1t2_failsafe", "T1+T2 fail: P2 forced", "Збій T1+T2: P2 примусово"},
    {"t2_failsafe", "T2 fail: P2 burn gate", "Збій T2: P2 за горінням"},
    {"t3_failsafe", "T3 fail: no offer", "Збій T3: без пропозиції"},
    {nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr},
    {"t1_fault", "T1 fault/unassigned", "Збій/не призначено T1"},
    {"t2_fault", "T2 fault/unassigned", "Збій/не призначено T2"},
    {"t3_fault", "T3 fault/unassigned", "Збій/не призначено T3"},
    {"t4_fault", "T4 fault/unassigned", "Збій/не призначено T4"},
    {"t5_fault", "T5 fault/unassigned", "Збій/не призначено T5"},
    {"t6_fault", "T6 fault/unassigned", "Збій/не призначено T6"},
};

uint32_t computeAlarmMask(const SensorInput t[BR_SENSOR_COUNT], bool overheat) {
    const bool f1 = classifySensor(t[BR_SENSOR_T1].state) == SensorHealth::Failed;
    const bool f2 = classifySensor(t[BR_SENSOR_T2].state) == SensorHealth::Failed;
    const bool f3 = classifySensor(t[BR_SENSOR_T3].state) == SensorHealth::Failed;

    uint32_t mask = 0;
    if (overheat) {
        mask |= 1u << BR_ALARM_OVERHEAT;
    }
    if (f1) {
        mask |= 1u << BR_ALARM_T1_FAILSAFE;
    }
    if (f1 && f2) {
        mask |= 1u << BR_ALARM_T1T2_FAILSAFE;
    }
    if (f2 && !f1) {
        mask |= 1u << BR_ALARM_T2_FAILSAFE;
    }
    if (f3) {
        mask |= 1u << BR_ALARM_T3_FAILSAFE;
    }
    for (uint8_t i = 0; i < BR_SENSOR_COUNT; ++i) {
        const SensorState s = t[i].state;
        if (s == SensorState::Unassigned || (s == SensorState::Fault && !t[i].missing)) {
            mask |= 1u << (BR_ALARM_SENSOR_BASE + i);
        }
    }
    return mask & BR_ALARM_OWNED_MASK;
}
