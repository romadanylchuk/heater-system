#pragma once
#include <stdint.h>
#include <HomeHeatingTypes.h>

// LITERAL copy of the "Fail-safe matrix" table of the stage-08 feature plan
// (feature-plan.md, section "Fail-safe matrix"). Data only, no logic: it is
// never computed from the code under test (D25). If a row disagrees with the
// controller, the controller is investigated -- this table is never edited to
// make a test pass.
//
// Classes: 'O' = Ok, 'F' = Fault (missing = false), 'W' = Unknown,
// 'U' = Unassigned (extra rows only). Fixture: heating enabled, defaults,
// H1 = 30, H2 = 40, H3 = 70, H4 = 50 when Ok; P4 relay actual == the expected P4
// request; estimate known after the boot recal; K1 idle, not inhibited.

struct FsRow {
    char h1, h2, h3;
    FailMode fail;
    bool p4On;
    P4Reason p4Reason;
    K1Mode k1Mode;
    uint32_t mask;
};

constexpr float FS_H1_C = 30.0f, FS_H2_C = 40.0f, FS_H3_C = 70.0f, FS_H4_C = 50.0f;

namespace fs {
constexpr FailMode None = FailMode::None, H1 = FailMode::H1, H2 = FailMode::H2, H3 = FailMode::H3,
                   Multi = FailMode::Multi;
constexpr bool ON = true, OFF = false;
constexpr P4Reason Demand = P4Reason::Demand, H3FaultForced = P4Reason::H3FaultForced,
                   SensorWait = P4Reason::SensorWait, MultiFaultOff = P4Reason::MultiFaultOff,
                   MultiFaultForced = P4Reason::MultiFaultForced, HeatingOff = P4Reason::HeatingOff;
constexpr K1Mode Normal = K1Mode::Normal, FailPosFeedback = K1Mode::FailPosFeedback, Closed = K1Mode::Closed,
                 FeedforwardOnly = K1Mode::FeedforwardOnly, FailPosFixed = K1Mode::FailPosFixed,
                 Wait = K1Mode::Wait, FeedbackOnly = K1Mode::FeedbackOnly;
}  // namespace fs

#define FS_ROW(a, b, c, fail, p4, reason, mode, mask) \
    {a, b, c, fs::fail, fs::p4, fs::reason, fs::mode, mask}

constexpr FsRow FAIL_SAFE_MATRIX[] = {
    //     H1   H2   H3   Fail   P4   P4 reason         K1 mode          Mask
    FS_ROW('O', 'O', 'O', None,  ON,  Demand,           Normal,          0x0000u),   // 1
    FS_ROW('O', 'O', 'F', H3,    ON,  H3FaultForced,    FailPosFeedback, 0x0401u),   // 2
    FS_ROW('O', 'O', 'W', None,  OFF, SensorWait,       Closed,          0x0000u),   // 3
    FS_ROW('O', 'F', 'O', H2,    ON,  Demand,           FeedforwardOnly, 0x0202u),   // 4
    FS_ROW('O', 'F', 'F', Multi, OFF, MultiFaultOff,    FailPosFixed,    0x0608u),   // 5
    FS_ROW('O', 'F', 'W', H2,    OFF, SensorWait,       Closed,          0x0202u),   // 6
    FS_ROW('O', 'W', 'O', None,  ON,  Demand,           Wait,            0x0000u),   // 7
    FS_ROW('O', 'W', 'F', H3,    ON,  H3FaultForced,    FailPosFeedback, 0x0401u),   // 8
    FS_ROW('O', 'W', 'W', None,  OFF, SensorWait,       Closed,          0x0000u),   // 9
    FS_ROW('F', 'O', 'O', H1,    ON,  Demand,           FeedbackOnly,    0x0104u),   // 10
    FS_ROW('F', 'O', 'F', Multi, OFF, MultiFaultOff,    FailPosFixed,    0x0508u),   // 11
    FS_ROW('F', 'O', 'W', H1,    OFF, SensorWait,       Closed,          0x0104u),   // 12
    FS_ROW('F', 'F', 'O', Multi, ON,  MultiFaultForced, FailPosFixed,    0x0308u),   // 13
    FS_ROW('F', 'F', 'F', Multi, OFF, MultiFaultOff,    FailPosFixed,    0x0708u),   // 14
    FS_ROW('F', 'F', 'W', Multi, OFF, SensorWait,       FailPosFixed,    0x0308u),   // 15
    FS_ROW('F', 'W', 'O', H1,    ON,  Demand,           Wait,            0x0104u),   // 16
    FS_ROW('F', 'W', 'F', Multi, OFF, MultiFaultOff,    FailPosFixed,    0x0508u),   // 17
    FS_ROW('F', 'W', 'W', H1,    OFF, SensorWait,       Closed,          0x0104u),   // 18
    FS_ROW('W', 'O', 'O', None,  ON,  Demand,           Wait,            0x0000u),   // 19
    FS_ROW('W', 'O', 'F', H3,    ON,  H3FaultForced,    FailPosFeedback, 0x0401u),   // 20
    FS_ROW('W', 'O', 'W', None,  OFF, SensorWait,       Closed,          0x0000u),   // 21
    FS_ROW('W', 'F', 'O', H2,    ON,  Demand,           Wait,            0x0202u),   // 22
    FS_ROW('W', 'F', 'F', Multi, OFF, MultiFaultOff,    FailPosFixed,    0x0608u),   // 23
    FS_ROW('W', 'F', 'W', H2,    OFF, SensorWait,       Closed,          0x0202u),   // 24
    FS_ROW('W', 'W', 'O', None,  ON,  Demand,           Wait,            0x0000u),   // 25
    FS_ROW('W', 'W', 'F', H3,    ON,  H3FaultForced,    FailPosFeedback, 0x0401u),   // 26
    FS_ROW('W', 'W', 'W', None,  OFF, SensorWait,       Closed,          0x0000u),   // 27
};
constexpr int FAIL_SAFE_MATRIX_ROWS = sizeof(FAIL_SAFE_MATRIX) / sizeof(FAIL_SAFE_MATRIX[0]);
static_assert(FAIL_SAFE_MATRIX_ROWS == 27, "fail-safe matrix has 27 rows");

// Extra literal rows (a)-(d). checkK2 = the plan states the K2 outcome.
struct FsExtra {
    const char* name;
    char h1, h2, h3, h4;
    bool heatingEnabled;
    FailMode fail;
    bool p4On;
    P4Reason p4Reason;
    K1Mode k1Mode;
    bool checkK2, k2Bypass;
    K2Reason k2Reason;
    bool checkNoNeed, noNeed;
    uint32_t mask;
};

constexpr FsExtra FAIL_SAFE_EXTRA[] = {
    // (a) fresh install: all four Unassigned.
    {"a_fresh_install", 'U', 'U', 'U', 'U', true, FailMode::Multi, false, P4Reason::MultiFaultOff,
     K1Mode::FailPosFixed, true, true, K2Reason::H4Fault, true, false, 0x0F18u},
    // (b) row 1 with heating disabled.
    {"b_row1_heating_off", 'O', 'O', 'O', 'O', false, FailMode::None, false, P4Reason::HeatingOff,
     K1Mode::Closed, false, false, K2Reason::None, false, false, 0x0000u},
    // (c) row 14 with heating disabled: alarms stay.
    {"c_row14_heating_off", 'F', 'F', 'F', 'O', false, FailMode::Multi, false, P4Reason::HeatingOff,
     K1Mode::Closed, false, false, K2Reason::None, false, false, 0x0708u},
    // (d) H4 Fault only: P4/K1 as row 1.
    {"d_h4_fault_only", 'O', 'O', 'O', 'F', true, FailMode::None, true, P4Reason::Demand,
     K1Mode::Normal, true, true, K2Reason::H4Fault, false, false, 0x0810u},
};
constexpr int FAIL_SAFE_EXTRA_ROWS = sizeof(FAIL_SAFE_EXTRA) / sizeof(FAIL_SAFE_EXTRA[0]);
