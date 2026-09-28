#include "K1Math.h"

namespace {

float clampPct(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 100.0f) return 100.0f;
    return v;
}

// Nearest-ms rounding of a non-negative ms value.
uint32_t roundMs(float ms) {
    if (ms <= 0.0f) return 0u;
    return static_cast<uint32_t>(ms + 0.5f);
}

// ms needed to travel deltaPct (>= 0) percent of the full stroke.
uint32_t msForPct(float deltaPct, uint32_t travelMs) {
    return roundMs(deltaPct / 100.0f * static_cast<float>(travelMs));
}

uint32_t minPulseMs(const HomeHeatingSettings& g) {
    return roundMs(g.k1MinPulseS * 1000.0f);
}

const K1Move NO_MOVE = {false, K1Direction::Close, 0u, false};

}  // namespace

void K1Estimator::reset() {
    _known = false;
    _pct = 0.0f;
}

void K1Estimator::applyMotion(const K1Motion& m, uint32_t travelMs) {
    if (travelMs == 0u) return;
    const int64_t netMs = static_cast<int64_t>(m.openMs) - static_cast<int64_t>(m.closeMs);
    if (netMs == 0) return;
    _pct = clampPct(_pct + static_cast<float>(netMs) * 100.0f / static_cast<float>(travelMs));
}

void K1Estimator::setKnown(float pct) {
    _known = true;
    _pct = clampPct(pct);
}

bool K1Estimator::known() const {
    return _known;
}

float K1Estimator::pct() const {
    return _pct;
}

float feedforwardTarget(float h1, float h2Set, float h3, float smallDiff) {
    const float diff = h3 - h1;
    if (diff <= smallDiff) return 100.0f;
    return clampPct((h2Set - h1) / diff * 100.0f);
}

K1Move planMoveTo(float fromPct, float targetPct, const HomeHeatingSettings& g) {
    const float from = clampPct(fromPct);
    const float target = clampPct(targetPct);
    const uint32_t travelMs = k1TravelMs(g);

    if (target <= 0.0f) {
        if (from <= 0.0f) return NO_MOVE;
        return K1Move{true, K1Direction::Close, msForPct(from, travelMs) + k1OverdriveMs(g), true};
    }
    if (target >= 100.0f) {
        if (from >= 100.0f) return NO_MOVE;
        return K1Move{true, K1Direction::Open, msForPct(100.0f - from, travelMs) + k1OverdriveMs(g), true};
    }
    const float delta = target - from;
    const uint32_t ms = msForPct(delta < 0.0f ? -delta : delta, travelMs);
    if (ms == 0u || ms < minPulseMs(g)) return NO_MOVE;
    return K1Move{true, delta > 0.0f ? K1Direction::Open : K1Direction::Close, ms, false};
}

K1Move planFeedback(float fromPct, float h2, float h2Set, const HomeHeatingSettings& g) {
    const float from = clampPct(fromPct);
    const float err = h2Set - h2;
    const float absErr = err < 0.0f ? -err : err;
    if (absErr <= g.k1Deadband) return NO_MOVE;

    float pulseS = g.k1Gain * absErr;
    const float maxS = static_cast<float>(g.k1MaxPulseS);
    if (pulseS > maxS) pulseS = maxS;
    const uint32_t ms = roundMs(pulseS * 1000.0f);
    if (ms == 0u || ms < minPulseMs(g)) return NO_MOVE;

    const uint32_t travelMs = k1TravelMs(g);
    const bool open = err > 0.0f;
    if (open && from >= 100.0f) return NO_MOVE;
    if (!open && from <= 0.0f) return NO_MOVE;

    const float movePct = travelMs == 0u ? 100.0f
                                          : static_cast<float>(ms) * 100.0f / static_cast<float>(travelMs);
    if (open && from + movePct >= 100.0f) {
        return K1Move{true, K1Direction::Open, msForPct(100.0f - from, travelMs) + k1OverdriveMs(g), true};
    }
    if (!open && from - movePct <= 0.0f) {
        return K1Move{true, K1Direction::Close, msForPct(from, travelMs) + k1OverdriveMs(g), true};
    }
    return K1Move{true, open ? K1Direction::Open : K1Direction::Close, ms, false};
}
