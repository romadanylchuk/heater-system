#pragma once
#include <stdint.h>
#include <BoilerRoomTypes.h>

// LITERAL copy of the "Sensor-state matrix" table of the stage-07 feature plan
// (feature-plan.md, section "Sensor-state matrix"). Data only, no logic: it is
// never computed from the code under test (D22). Shared by test_ctl_matrix and
// test_ctl_runtime. If a row disagrees with the controller, the controller is
// investigated -- this table is never edited to make a test pass.
//
// Classes: 'O' = Ok, 'F' = Failed (run as Fault(missing=false) AND Unassigned),
// 'W' = Unknown. Scenarios (when Ok): HOT T1 75, COLD T1 30, OH T1 95; T2 50,
// T3 60; T4/T5/T6 Ok 55/50/45; default settings; gated flag = true; linkUp;
// p3RelayOn = false; p3LastRunMs = 0; single update() at nowMs = 1000 on a fresh
// controller. "C" = control slot (lock applies), "S" = safety slot (bypass).

struct MxPump { bool on; bool safety; PumpReason reason; };
struct MxScenario { MxPump p1, p2, p3; uint32_t bits04; };   // bits04: expected alarm bits 0..4
struct MxRow { char t1, t2, t3; MxScenario sc[3]; };        // sc[MX_HOT], sc[MX_COLD], sc[MX_OH]

constexpr int MX_HOT = 0, MX_COLD = 1, MX_OH = 2, MX_SCENARIO_COUNT = 3;
constexpr float MX_T1_C[MX_SCENARIO_COUNT] = {75.0f, 30.0f, 95.0f};
constexpr float MX_T2_C = 50.0f, MX_T3_C = 60.0f, MX_T4_C = 55.0f, MX_T5_C = 50.0f, MX_T6_C = 45.0f;
constexpr uint64_t MX_NOW_MS = 1000;

// Plan reason names -> PumpReason (mapping given verbatim in the plan).
namespace mx {
constexpr PumpReason charge = PumpReason::Charge, chargeT1 = PumpReason::ChargeT1Only,
                     overheat = PumpReason::Overheat, t1Fault = PumpReason::T1FaultForced,
                     wait = PumpReason::SensorWait, return_ = PumpReason::Return,
                     returnT2 = PumpReason::ReturnT2Only, burnGate = PumpReason::ReturnBurnGate,
                     t1t2Fault = PumpReason::T1T2FaultForced, off = PumpReason::SupplyOff,
                     normal = PumpReason::SupplyNormal, dump = PumpReason::OverheatDump;
}  // namespace mx

constexpr MxPump mxPump(bool on, bool safety, PumpReason r) { return MxPump{on, safety, r}; }
constexpr MxScenario mxSc(MxPump p1, MxPump p2, MxPump p3, uint32_t bits) { return MxScenario{p1, p2, p3, bits}; }
#define MX_ON_C(r) mxPump(true, false, mx::r)
#define MX_ON_S(r) mxPump(true, true, mx::r)
#define MX_OFF_C(r) mxPump(false, false, mx::r)
#define MX_B(n) (1u << (n))
#define MX_NONE 0u
// One scenario cell: P1, P2, P3, alarm bits 0-4.
#define MX_SC(p1, p2, p3, bits) mxSc(p1, p2, p3, bits)
// A row whose cells are single-valued (T1 not Ok): the same outcome in HOT/COLD/OH.
#define MX_SAME(p1, p2, p3, bits) {MX_SC(p1, p2, p3, bits), MX_SC(p1, p2, p3, bits), MX_SC(p1, p2, p3, bits)}

constexpr MxRow SENSOR_MATRIX[] = {
    // #1 O O O
    {'O', 'O', 'O', {MX_SC(MX_ON_C(charge),   MX_ON_C(return_),  MX_OFF_C(off), MX_NONE),
                     MX_SC(MX_OFF_C(charge),  MX_OFF_C(return_), MX_OFF_C(off), MX_NONE),
                     MX_SC(MX_ON_S(overheat), MX_ON_C(return_),  MX_ON_S(dump), MX_B(0))}},
    // #2 O O F
    {'O', 'O', 'F', {MX_SC(MX_ON_C(chargeT1),  MX_ON_C(return_),  MX_ON_C(normal), MX_B(4)),
                     MX_SC(MX_OFF_C(chargeT1), MX_OFF_C(return_), MX_ON_C(normal), MX_B(4)),
                     MX_SC(MX_ON_S(overheat),  MX_ON_C(return_),  MX_ON_S(dump),   MX_B(0) | MX_B(4))}},
    // #3 O O W
    {'O', 'O', 'W', {MX_SC(MX_OFF_C(wait),    MX_ON_C(return_),  MX_ON_C(normal), MX_NONE),
                     MX_SC(MX_OFF_C(wait),    MX_OFF_C(return_), MX_ON_C(normal), MX_NONE),
                     MX_SC(MX_ON_S(overheat), MX_ON_C(return_),  MX_ON_S(dump),   MX_B(0))}},
    // #4 O F O
    {'O', 'F', 'O', {MX_SC(MX_ON_C(charge),   MX_ON_C(burnGate),  MX_OFF_C(off), MX_B(3)),
                     MX_SC(MX_OFF_C(charge),  MX_OFF_C(burnGate), MX_OFF_C(off), MX_B(3)),
                     MX_SC(MX_ON_S(overheat), MX_ON_C(burnGate),  MX_ON_S(dump), MX_B(0) | MX_B(3))}},
    // #5 O F F
    {'O', 'F', 'F', {MX_SC(MX_ON_C(chargeT1),  MX_ON_C(burnGate),  MX_ON_C(normal), MX_B(3) | MX_B(4)),
                     MX_SC(MX_OFF_C(chargeT1), MX_OFF_C(burnGate), MX_ON_C(normal), MX_B(3) | MX_B(4)),
                     MX_SC(MX_ON_S(overheat),  MX_ON_C(burnGate),  MX_ON_S(dump),   MX_B(0) | MX_B(3) | MX_B(4))}},
    // #6 O F W
    {'O', 'F', 'W', {MX_SC(MX_OFF_C(wait),    MX_ON_C(burnGate),  MX_ON_C(normal), MX_B(3)),
                     MX_SC(MX_OFF_C(wait),    MX_OFF_C(burnGate), MX_ON_C(normal), MX_B(3)),
                     MX_SC(MX_ON_S(overheat), MX_ON_C(burnGate),  MX_ON_S(dump),   MX_B(0) | MX_B(3))}},
    // #7 O W O
    {'O', 'W', 'O', {MX_SC(MX_ON_C(charge),   MX_OFF_C(wait), MX_OFF_C(off), MX_NONE),
                     MX_SC(MX_OFF_C(charge),  MX_OFF_C(wait), MX_OFF_C(off), MX_NONE),
                     MX_SC(MX_ON_S(overheat), MX_OFF_C(wait), MX_ON_S(dump), MX_B(0))}},
    // #8 O W F
    {'O', 'W', 'F', {MX_SC(MX_ON_C(chargeT1),  MX_OFF_C(wait), MX_ON_C(normal), MX_B(4)),
                     MX_SC(MX_OFF_C(chargeT1), MX_OFF_C(wait), MX_ON_C(normal), MX_B(4)),
                     MX_SC(MX_ON_S(overheat),  MX_OFF_C(wait), MX_ON_S(dump),   MX_B(0) | MX_B(4))}},
    // #9 O W W
    {'O', 'W', 'W', {MX_SC(MX_OFF_C(wait),    MX_OFF_C(wait), MX_ON_C(normal), MX_NONE),
                     MX_SC(MX_OFF_C(wait),    MX_OFF_C(wait), MX_ON_C(normal), MX_NONE),
                     MX_SC(MX_ON_S(overheat), MX_OFF_C(wait), MX_ON_S(dump),   MX_B(0))}},
    // #10..#18: T1 Failed -> single value for HOT/COLD/OH.
    {'F', 'O', 'O', MX_SAME(MX_ON_S(t1Fault), MX_ON_C(returnT2),  MX_OFF_C(off),   MX_B(1))},
    {'F', 'O', 'F', MX_SAME(MX_ON_S(t1Fault), MX_ON_C(returnT2),  MX_ON_C(normal), MX_B(1) | MX_B(4))},
    {'F', 'O', 'W', MX_SAME(MX_ON_S(t1Fault), MX_ON_C(returnT2),  MX_ON_C(normal), MX_B(1))},
    {'F', 'F', 'O', MX_SAME(MX_ON_S(t1Fault), MX_ON_S(t1t2Fault), MX_OFF_C(off),   MX_B(1) | MX_B(2))},
    {'F', 'F', 'F', MX_SAME(MX_ON_S(t1Fault), MX_ON_S(t1t2Fault), MX_ON_C(normal), MX_B(1) | MX_B(2) | MX_B(4))},
    {'F', 'F', 'W', MX_SAME(MX_ON_S(t1Fault), MX_ON_S(t1t2Fault), MX_ON_C(normal), MX_B(1) | MX_B(2))},
    {'F', 'W', 'O', MX_SAME(MX_ON_S(t1Fault), MX_OFF_C(wait),     MX_OFF_C(off),   MX_B(1))},
    {'F', 'W', 'F', MX_SAME(MX_ON_S(t1Fault), MX_OFF_C(wait),     MX_ON_C(normal), MX_B(1) | MX_B(4))},
    {'F', 'W', 'W', MX_SAME(MX_ON_S(t1Fault), MX_OFF_C(wait),     MX_ON_C(normal), MX_B(1))},
    // #19..#27: T1 Unknown -> single value for HOT/COLD/OH.
    {'W', 'O', 'O', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_OFF_C(off),   MX_NONE)},
    {'W', 'O', 'F', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_ON_C(normal), MX_B(4))},
    {'W', 'O', 'W', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_ON_C(normal), MX_NONE)},
    {'W', 'F', 'O', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_OFF_C(off),   MX_B(3))},
    {'W', 'F', 'F', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_ON_C(normal), MX_B(3) | MX_B(4))},
    {'W', 'F', 'W', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_ON_C(normal), MX_B(3))},
    {'W', 'W', 'O', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_OFF_C(off),   MX_NONE)},
    {'W', 'W', 'F', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_ON_C(normal), MX_B(4))},
    {'W', 'W', 'W', MX_SAME(MX_OFF_C(wait), MX_OFF_C(wait), MX_ON_C(normal), MX_NONE)},
};
constexpr int SENSOR_MATRIX_ROWS = sizeof(SENSOR_MATRIX) / sizeof(SENSOR_MATRIX[0]);
static_assert(SENSOR_MATRIX_ROWS == 27, "plan matrix has 27 rows");
