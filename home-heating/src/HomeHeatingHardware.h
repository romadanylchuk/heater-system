#pragma once
#include <HwConfig.h>

// home-heating's constexpr hardware descriptor (stage 03). Relay map: R1 = K2,
// R2 = K1 motor power, R3 = K1 direction, R4 = P4, R5-R6 spare (forced OFF, not
// in the table below). K1 channels are not lockable/logChanges (the K1Driver
// owns their timing and pulses would flood the event log, D10).
inline constexpr RelayChannelDesc HOME_HEATING_RELAYS[] = {
    {0, "K2", RelayRole::Diverter, true, true},
    {1, "K1 power", RelayRole::K1Power, false, false},
    {2, "K1 direction", RelayRole::K1Direction, false, false},
    {3, "P4", RelayRole::Pump, true, true},
};

inline constexpr LogicalSensorDesc HOME_HEATING_SENSORS[] = {
    {"H1", "sensorH1"},
    {"H2", "sensorH2"},
    {"H3", "sensorH3"},
    {"H4", "sensorH4"},
};

inline constexpr AntiSeizeOutputDesc HOME_HEATING_ANTI_SEIZE[] = {
    {"P4", AntiSeizeKind::Pump, 3, "asEnP4", NO_RELAY_CHANNEL},
    {"K2", AntiSeizeKind::Toggle, 0, "asEnK2", NO_RELAY_CHANNEL},
    {"K1", AntiSeizeKind::ValveStroke, 1, "asEnK1", 3},
};

inline constexpr K1Wiring HOME_HEATING_K1 = {true, 1, 2};

inline constexpr HwProjectConfig HOME_HEATING_HW = {
    HOME_HEATING_RELAYS, 4,
    HOME_HEATING_SENSORS, 4,
    HOME_HEATING_ANTI_SEIZE, 3,
    HOME_HEATING_K1,
};
