#pragma once
#include <stdint.h>
#include "HomeHeatingControlSettings.h"
#include "HomeHeatingTypes.h"
#include "K1Math.h"

// K1 mixing-valve control state machine (stage 08, C7). Pure; one update() per
// tick, time injected. Sequence per tick:
//  1. integrate the tick's motion into the estimate; during recal count actual
//     CLOSE ms (any OPEN motion restarts the count, D6);
//  2. recal starts on boot (first update after reset()), on the P4 request
//     falling edge and on heating enabled->disabled (D7); its CLOSE is issued
//     the same tick even over a running Control pulse, unless inhibited or
//     anti-seize owns K1 (then at the first allowed tick);
//  3. canMove = !busy && !inhibited && !antiSeizeOwned; inhibit end re-arms FF (D15);
//  4. recal ends once the remaining CLOSE is < 1 s (estimate known at 0 %);
//  5. mode selection (fail-safe table, D10, D12); a mode change re-arms FF;
//  6. actions: Closed/FailPosFixed track their target every tick; periodic
//     modes evaluate on mode entry / every k1Period while idle (A1, D5).
//     Hold (stage 09 C13, D12): while in.hold the periodic FF/feedback
//     evaluation is skipped (no command, period timer untouched); recal,
//     Closed/FailPosFixed tracking and the FailPosFeedback anchor are never
//     held. The hold falling edge re-arms FF and forces an evaluation at the
//     first idle tick ("as after a mode entry", A1);
//  7. outputs and edge memory.

// Which part of the logic produced K1Decision.cmd (stage 09 C13, D11).
// None whenever !cmd.issue.
enum class K1CmdKind : uint8_t { None = 0, Recal = 1, Position = 2, Feedforward = 3, Feedback = 4 };

struct K1Inputs {
    bool heatingEnabled;
    bool p4Requested;           // this tick's P4Decision.on
    bool p4Running;             // p4Requested && P4 relay actual ON (D10)
    FailMode fail;
    SensorHealth h1, h2, h3;
    float h1C, h2C, h3C;
    bool k1Busy;                // K1Driver::busy()
    bool k1AntiSeizeOwned;      // K1Driver::owner() == K1Owner::AntiSeize
    bool inhibited;             // RelayBank::inhibited() (OTA)
    K1Motion motion;            // K1Driver::takeMotion() of this tick
    bool hold = false;          // stage 09: K1 step test running (C13); appended last
};

struct K1Decision {
    K1Command cmd;
    K1Mode mode;
    bool known;
    float posPct;
    bool recalStarted, recalEnded;   // one-tick edge flags
    bool ffValid;                    // a feed-forward target has been applied in this mode
    float ffAppliedPct;              // last applied feed-forward target (display)
    K1CmdKind cmdKind = K1CmdKind::None;   // stage 09 (C13, D11); appended last
};

class K1Logic {
public:
    // Estimator unknown, not recalibrating, all flags cleared, prev edges false.
    void reset();
    // g must already be guarded.
    K1Decision update(const K1Inputs& in, const HomeHeatingSettings& g, uint64_t nowMs);

private:
    K1Estimator _est;
    bool _booted = false;
    bool _recalibrating = false;
    uint32_t _recalCloseMs = 0;
    K1Mode _mode = K1Mode::Unknown;
    bool _ffApplied = false;
    float _xApplied = 0.0f;
    bool _anchorApplied = false;
    bool _needEval = true;
    uint64_t _lastEvalMs = 0;
    bool _prevP4Requested = false;
    bool _prevHeating = false;
    bool _prevInhibited = false;
    bool _prevHold = false;
};
