#include "HomeHeatingAlarms.h"

const AlarmDescriptor HOME_HEATING_ALARMS[HOME_HEATING_ALARM_COUNT] = {
    {"h3_failsafe", "H3 fail: K1 fixed", "Збій H3: K1 фіксовано"},
    {"h2_failsafe", "H2 fail: K1 FF only", "Збій H2: K1 лише пряме"},
    {"h1_failsafe", "H1 fail: K1 FB only", "Збій H1: K1 лише зворотне"},
    {"multi_failsafe", "Sensors fail: K1 fix", "Збій датчиків: K1 фікс."},
    {"h4_failsafe", "H4 fail: K2 bypass", "Збій H4: K2 байпас"},
    {nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr},
    {"h1_fault", "H1 fault/unassigned", "Збій/не призначено H1"},
    {"h2_fault", "H2 fault/unassigned", "Збій/не призначено H2"},
    {"h3_fault", "H3 fault/unassigned", "Збій/не призначено H3"},
    {"h4_fault", "H4 fault/unassigned", "Збій/не призначено H4"},
};

uint32_t computeAlarmMask(const SensorInput h[HH_SENSOR_COUNT], FailMode fail) {
    uint32_t mask = 0;
    switch (fail) {
        case FailMode::H3:
            mask |= 1u << HH_ALARM_H3_FAILSAFE;
            break;
        case FailMode::H2:
            mask |= 1u << HH_ALARM_H2_FAILSAFE;
            break;
        case FailMode::H1:
            mask |= 1u << HH_ALARM_H1_FAILSAFE;
            break;
        case FailMode::Multi:
            mask |= 1u << HH_ALARM_MULTI_FAILSAFE;
            break;
        default:
            break;
    }
    if (classifySensor(h[HH_SENSOR_H4].state) == SensorHealth::Failed) {
        mask |= 1u << HH_ALARM_H4_FAILSAFE;
    }
    for (uint8_t i = 0; i < HH_SENSOR_COUNT; ++i) {
        const SensorState s = h[i].state;
        if (s == SensorState::Unassigned || (s == SensorState::Fault && !h[i].missing)) {
            mask |= 1u << (HH_ALARM_SENSOR_BASE + i);
        }
    }
    return mask & HH_ALARM_OWNED_MASK;
}
