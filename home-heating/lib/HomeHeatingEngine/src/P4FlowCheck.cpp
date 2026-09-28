#include "P4FlowCheck.h"

void P4FlowCheck::reset() {
    _tracking = false;
    _active = false;
    _startMs = 0;
}

bool P4FlowCheck::update(const P4FlowInputs& in, const P4FlowParams& p, uint64_t nowMs) {
    const bool paused = !(in.h1Ok && in.h2Ok && in.h3Ok) || !in.k1Known;
    if (!in.p4On || paused) {
        // P4 off, or a pause (non-Ok sensor / K1 unknown, A3/A4/D5): the timer
        // restarts at the next ON edge / recovery.
        _tracking = false;
        _active = false;
        return _active;
    }
    if (!_tracking) {
        _tracking = true;
        _startMs = nowMs;
    }
    const uint64_t minOnMs = static_cast<uint64_t>(p.minOnMin) * 60000ull;
    const bool longEnough = nowMs >= _startMs && (nowMs - _startMs) >= minOnMs;
    const bool cond = longEnough && (in.k1PosPct > static_cast<float>(p.k1MinPct)) &&
                      (in.h3C > in.h1C + p.deltaC) && ((in.h2C - in.h1C) < p.minDiffC);
    _active = p.enabled && cond;
    return _active;
}
