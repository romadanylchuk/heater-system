#pragma once
#include <HwConfig.h>

// boiler-room's constexpr hardware descriptor (stage 03). Relay map: R1 = P1,
// R2 = P2, R3 = P3, R4-R6 spare (forced OFF, not in the table below). No K1 on
// this controller.
inline constexpr RelayChannelDesc BOILER_ROOM_RELAYS[] = {
    {0, "P1", RelayRole::Pump, true, true},
    {1, "P2", RelayRole::Pump, true, true},
    {2, "P3", RelayRole::Pump, true, true},
};

inline constexpr LogicalSensorDesc BOILER_ROOM_SENSORS[] = {
    {"T1", "sensorT1"},
    {"T2", "sensorT2"},
    {"T3", "sensorT3"},
    {"T4", "sensorT4"},
    {"T5", "sensorT5"},
    {"T6", "sensorT6"},
};

inline constexpr AntiSeizeOutputDesc BOILER_ROOM_ANTI_SEIZE[] = {
    {"P1", AntiSeizeKind::Pump, 0, "asEnP1", NO_RELAY_CHANNEL},
    {"P2", AntiSeizeKind::Pump, 1, "asEnP2", NO_RELAY_CHANNEL},
    {"P3", AntiSeizeKind::Pump, 2, "asEnP3", NO_RELAY_CHANNEL},
};

inline constexpr K1Wiring BOILER_ROOM_K1 = {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL};

inline constexpr HwProjectConfig BOILER_ROOM_HW = {
    BOILER_ROOM_RELAYS, 3,
    BOILER_ROOM_SENSORS, 6,
    BOILER_ROOM_ANTI_SEIZE, 3,
    BOILER_ROOM_K1,
};
