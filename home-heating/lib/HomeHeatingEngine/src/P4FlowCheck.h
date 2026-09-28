#pragma once
#include <stdint.h>

// H1 "P4 no flow" check (stage 09, C11, A3): P4 actually ON >= minOn, K1 open
// more than k1Min %, H3 hotter than H1 by more than delta, yet H2 barely above
// H1 (H2 - H1 < minDiff). Instantaneous: no rise/baseline term. Pure, time
// injected. It only produces a warning flag, never a controller input (D2).
struct P4FlowParams {
    bool enabled;
    uint32_t minOnMin;
    uint32_t k1MinPct;
    float deltaC;
    float minDiffC;
};

struct P4FlowInputs {
    bool p4On;               // relay ACTUAL state (A1), not the request
    bool h1Ok, h2Ok, h3Ok;   // sensor state == SensorState::Ok (D3)
    float h1C, h2C, h3C;
    bool k1Known;            // K1 position estimate known
    float k1PosPct;          // estimator value, not the live projection
};

class P4FlowCheck {
public:
    void reset();  // not tracking, inactive

    // Returns active(). Rules, in order:
    //  - !p4On -> stop tracking, inactive.
    //  - p4On && paused (any of H1..H3 not Ok, or !k1Known) -> stop tracking
    //    (A3/A4, D5: the timer restarts at recovery), inactive.
    //  - p4On && !paused && !tracking -> start (startMs = now).
    //  - tracking -> cond = (now - startMs >= minOnMin * 60000) &&
    //    (k1PosPct > k1MinPct) && (h3C > h1C + deltaC) && (h2C - h1C < minDiffC).
    //  - active = params.enabled && cond (D4: tracking continues while disabled).
    bool update(const P4FlowInputs& in, const P4FlowParams& p, uint64_t nowMs);

    bool active() const { return _active; }
    bool tracking() const { return _tracking; }

private:
    bool _tracking = false;
    bool _active = false;
    uint64_t _startMs = 0;
};
