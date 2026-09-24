#pragma once
#include <stddef.h>
#include <stdint.h>
#include <HardwareStatus.h>

// Per-project hardware descriptor: which relay channels exist and what they
// drive, the logical-sensor <-> settings-key mapping, the anti-seize eligible
// outputs, and the K1 wiring (if any). Each project builds one constexpr
// HwProjectConfig (BoilerRoomHardware.h / HomeHeatingHardware.h); HwRuntime
// validates and consumes it. Pure data + one pure validation function.
enum class RelayRole : uint8_t { Pump, Diverter, K1Power, K1Direction };

struct RelayChannelDesc {
    uint8_t channel;
    const char* name;
    RelayRole role;
    bool lockable;
    bool logChanges;
};

struct LogicalSensorDesc {
    const char* name;         // "T1".. / "H1"..
    const char* settingKey;   // e.g. "sensorT1"
};

enum class AntiSeizeKind : uint8_t { Pump, Toggle, ValveStroke };

struct AntiSeizeOutputDesc {
    const char* name;             // "P1", "K2", "K1"
    AntiSeizeKind kind;
    uint8_t channel;              // relay channel (Pump/Toggle); K1 power channel for ValveStroke
    const char* enableKey;        // Bool setting key, e.g. "asEnP1"
    uint8_t blockWhileOnChannel;  // run only while this channel is OFF (K1: P4's channel), else NO_RELAY_CHANNEL
};

struct K1Wiring {
    bool present;
    uint8_t powerChannel;
    uint8_t directionChannel;
};

struct HwProjectConfig {
    const RelayChannelDesc* relays;
    size_t relayCount;
    const LogicalSensorDesc* sensors;
    size_t sensorCount;
    const AntiSeizeOutputDesc* antiSeize;
    size_t antiSeizeCount;
    K1Wiring k1;
};

// Pure structural check (no settings lookup):
// - relayCount <= RELAY_CHANNEL_COUNT; each channel < RELAY_CHANNEL_COUNT, unique, name non-null.
// - K1Power/K1Direction roles are exactly the k1 channels when k1.present (and not lockable);
//   no K1 roles when !k1.present.
// - sensorCount <= MAX_LOGICAL_SENSORS, name/settingKey non-null, settingKey unique.
// - antiSeizeCount <= MAX_ANTI_SEIZE_OUTPUTS, enableKey non-null/unique;
//   Pump/Toggle channel present in relays with role Pump/Diverter;
//   ValveStroke requires k1.present and channel == k1.powerChannel;
//   blockWhileOnChannel is NO_RELAY_CHANNEL or a configured channel.
bool validateHwConfig(const HwProjectConfig& cfg);
