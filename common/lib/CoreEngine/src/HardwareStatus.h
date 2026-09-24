#pragma once
#include <stddef.h>
#include <stdint.h>
#include <BoardConfig.h>

// POD hardware-status types shared by CommonState and HwEngine (stage 03). These
// live in CoreEngine (not HwEngine) because CommonState must hold them and
// CoreEngine cannot depend on HwEngine (D1). Pure data only: no methods, no
// hardware calls.
constexpr size_t  MAX_LOGICAL_SENSORS    = 8;
constexpr size_t  MAX_ANTI_SEIZE_OUTPUTS = 4;
constexpr uint8_t NO_LOGICAL_SENSOR      = 0xFF;
constexpr uint8_t NO_RELAY_CHANNEL       = 0xFF;

// Common hardware alarm bits in AlarmStatus.activeMask: bit (24 + logical index)
// = "assigned sensor missing". Controller alarm tables (stages 07/08) use bits
// 0..23 (D18).
constexpr uint8_t  SENSOR_MISSING_ALARM_BIT_BASE = 24;
constexpr uint32_t SENSOR_MISSING_ALARM_MASK     = 0xFF000000u;

// DiagnosticWarning event source = EVENT_SOURCE_DIAG_BASE + code.
constexpr uint16_t DIAG_CODE_RELAY_IO         = 1;
constexpr uint16_t DIAG_CODE_ONEWIRE_OVERFLOW = 2;
constexpr uint16_t DIAG_CODE_ANTISEIZE_K1_STROKE = 3;  // anti-seize K1 close leg could not start (invalid stroke)

enum class RelayReason : uint8_t { None = 0, Boot, Control, Safety, AntiSeize, K1Drive };
enum class SensorState : uint8_t { Unassigned = 0, Unknown, Ok, Fault };

struct RelayChannelStatus {
    bool requested;           // effective requested state (after slot arbitration)
    bool safety;               // effective request comes from the safety slot
    bool lockDelayed;          // requested != actual and held by the lock
    uint16_t lockRemainingS;   // ceil seconds until the lock expires (0 if not delayed)
    RelayReason reason;        // reason of the last actual change (Boot at start)
};

struct LogicalSensorStatus {
    SensorState state;
    float tempC;                // valid only when state == Ok (else NAN)
    bool assigned;
    bool missing;
    uint8_t address[8];         // all-zero when unassigned
};

struct SensorArray {
    uint8_t count;
    LogicalSensorStatus sensor[MAX_LOGICAL_SENSORS];
};

struct K1Status {
    bool present;
    bool busy;
    bool powerOn;
    bool directionOpen;
    uint32_t currentRunMs;   // run time of the pulse in progress (0 if none)
};

struct AntiSeizeOutputStatus {
    bool enabled;
    bool pending;
    bool running;
    uint32_t idleS;
};

struct AntiSeizeStatus {
    uint8_t count;
    AntiSeizeOutputStatus output[MAX_ANTI_SEIZE_OUTPUTS];
};
