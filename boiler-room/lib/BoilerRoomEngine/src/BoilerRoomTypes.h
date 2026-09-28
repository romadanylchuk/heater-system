#pragma once
#include <stddef.h>
#include <stdint.h>
#include <HardwareStatus.h>

// Shared POD types of the boiler-room controller (stage 07, C1). Pure: no
// hardware, no time source. Pump indices equal the RelayBank channels 0..2 and
// sensor indices equal the logical sensor indices T1..T6.
constexpr uint8_t BR_PUMP_P1 = 0, BR_PUMP_P2 = 1, BR_PUMP_P3 = 2, BR_PUMP_COUNT = 3;   // == relay channels 0..2
constexpr uint8_t BR_SENSOR_T1 = 0, BR_SENSOR_T2 = 1, BR_SENSOR_T3 = 2, BR_SENSOR_T4 = 3, BR_SENSOR_T5 = 4,
                  BR_SENSOR_T6 = 5, BR_SENSOR_COUNT = 6;                               // == logical sensor indices

// Failed = Fault | Unassigned (an unassigned sensor is not working), Pending =
// Unknown (boot / reassignment, before the debounce settles: hold, not a fault).
enum class SensorHealth : uint8_t { Ok, Failed, Pending };
SensorHealth classifySensor(SensorState s);

struct SensorInput {            // one logical sensor as the controller sees it
    SensorState state;          // from CommonState.sensors.sensor[i].state
    float tempC;                // meaningful only when state == Ok
    bool missing;               // CommonState ... .missing (stage-03 bit 24+i owns this condition)
};

// Stable numeric codes (logged as event values): never renumber.
enum class PumpReason : uint8_t {
    None = 0, SensorWait = 1, Charge = 2, ChargeT1Only = 3, Overheat = 4, T1FaultForced = 5,
    Return = 6, ReturnT2Only = 7, ReturnBurnGate = 8, T1T2FaultForced = 9,
    SupplyNormal = 10, SupplyOff = 11, SupplyOffer = 12, AntiFreeze = 13, OverheatDump = 14,
};
enum class P3Mode : uint8_t { Normal = 0, Off = 1, Offer = 2 };

struct PumpDecision {
    bool on;              // effective request
    bool safety;          // true => safety slot (bypasses the relay lock); only ever with on == true
    PumpReason reason;
    bool controlOn;       // value for the control slot underneath (== on when !safety)
};

const char* pumpReasonKey(PumpReason r);    // JSON/lang key
const char* pumpReasonShort(PumpReason r);  // ASCII, <= 9 chars, OLED
const char* p3ModeKey(P3Mode m);            // "normal" | "off" | "offer"
const char* p3ModeShort(P3Mode m);          // "NORMAL" | "OFF" | "OFFER"
