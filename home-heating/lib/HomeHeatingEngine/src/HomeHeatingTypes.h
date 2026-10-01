#pragma once
#include <stddef.h>
#include <stdint.h>
#include <HardwareStatus.h>
#include <K1Driver.h>

// Shared POD types of the home-heating controller (stage 08, C1). Pure: no
// hardware, no time source. Sensor indices equal the logical sensor indices
// H1..H4, relay indices equal the HOME_HEATING_RELAYS channels and anti-seize
// indices equal the HOME_HEATING_ANTI_SEIZE order.
constexpr uint8_t HH_SENSOR_H1 = 0, HH_SENSOR_H2 = 1, HH_SENSOR_H3 = 2, HH_SENSOR_H4 = 3, HH_SENSOR_COUNT = 4;
constexpr uint8_t HH_RELAY_K2 = 0, HH_RELAY_K1_POWER = 1, HH_RELAY_K1_DIR = 2, HH_RELAY_P4 = 3;
constexpr uint8_t HH_AS_P4 = 0, HH_AS_K2 = 1, HH_AS_K1 = 2;

// K2 relay polarity. The installed diverter is normally-open to BYPASS: with no
// power it rests on BYPASS, and the R1 NO contact powers it to TANK. So relay
// energised = TANK, de-energised = BYPASS (also the boot / OTA / not-ready state).
// Everything that maps K2 <-> relay goes through these two helpers.
constexpr bool HH_K2_RELAY_ON_IS_TANK = true;
constexpr bool hhK2RelayForBypass(bool bypass) { return HH_K2_RELAY_ON_IS_TANK ? !bypass : bypass; }
constexpr bool hhK2BypassFromRelay(bool relayOn) { return HH_K2_RELAY_ON_IS_TANK ? !relayOn : relayOn; }

// Failed = Fault | Unassigned (an unassigned sensor is not working), Pending =
// Unknown (boot / reassignment, before the debounce settles: wait, not a fault).
enum class SensorHealth : uint8_t { Ok, Failed, Pending };
SensorHealth classifySensor(SensorState s);

struct SensorInput {            // one logical sensor as the controller sees it
    SensorState state;          // from CommonState.sensors.sensor[i].state
    float tempC;                // meaningful only when state == Ok
    bool missing;               // CommonState ... .missing (stage-03 bit 24+i owns this condition)
};

// Stable numeric codes (logged as event values): never renumber.
enum class P4Reason : uint8_t {
    None = 0, HeatingOff = 1, SensorWait = 2, Demand = 3, OffDelay = 4,
    SupplyCold = 5, H3FaultForced = 6, MultiFaultForced = 7, MultiFaultOff = 8,
};
enum class K2Reason : uint8_t {
    None = 0, SensorWait = 1, Charging = 2, DeltaLow = 3, H3Low = 4, H4Full = 5, H3Fault = 6, H4Fault = 7,
};
enum class K1Mode : uint8_t {
    Unknown = 0, Recalibrating = 1, Closed = 2, Wait = 3, Normal = 4,
    FeedbackOnly = 5, FeedforwardOnly = 6, FailPosFeedback = 7, FailPosFixed = 8,
};
// H1..H3 = the single-sensor rows of the brief's fail-safe table; Multi = two or more of H1..H3 failed.
enum class FailMode : uint8_t { None = 0, H1 = 1, H2 = 2, H3 = 3, Multi = 4 };

// >= 2 Failed -> Multi, else H3, H2, H1 (first Failed in that order), else None.
// Pending never counts as failed.
FailMode computeFailMode(SensorHealth h1, SensorHealth h2, SensorHealth h3);

struct K1Command {
    bool issue;
    K1Direction dir;
    uint32_t ms;
};

// key: snake_case ASCII (JSON / lang / HA); short: ASCII <= 8 chars (OLED).
const char* p4ReasonKey(P4Reason r);
const char* p4ReasonShort(P4Reason r);
const char* k2ReasonKey(K2Reason r);
const char* k2ReasonShort(K2Reason r);
const char* k1ModeKey(K1Mode m);
const char* k1ModeShort(K1Mode m);
const char* failModeKey(FailMode f);   // "none" | "h1" | "h2" | "h3" | "multi"
