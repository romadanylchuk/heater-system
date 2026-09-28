#include "K1Logic.h"

namespace {

const K1Command NO_COMMAND = {false, K1Direction::Close, 0u};

K1Command toCommand(const K1Move& m) {
    if (!m.issue) return NO_COMMAND;
    return K1Command{true, m.dir, m.ms};
}

K1Mode selectMode(const K1Inputs& in) {
    if (!in.heatingEnabled) return K1Mode::Closed;
    if (in.fail == FailMode::Multi) return K1Mode::FailPosFixed;
    if (!in.p4Running) return K1Mode::Closed;
    switch (in.fail) {
        case FailMode::H3:
            return K1Mode::FailPosFeedback;
        case FailMode::H2:
            return (in.h1 == SensorHealth::Ok && in.h3 == SensorHealth::Ok) ? K1Mode::FeedforwardOnly
                                                                            : K1Mode::Wait;
        case FailMode::H1:
            return in.h2 == SensorHealth::Ok ? K1Mode::FeedbackOnly : K1Mode::Wait;
        default:
            return (in.h1 == SensorHealth::Ok && in.h2 == SensorHealth::Ok) ? K1Mode::Normal : K1Mode::Wait;
    }
}

}  // namespace

void K1Logic::reset() {
    _est.reset();
    _booted = false;
    _recalibrating = false;
    _recalCloseMs = 0;
    _mode = K1Mode::Unknown;
    _ffApplied = false;
    _xApplied = 0.0f;
    _anchorApplied = false;
    _needEval = true;
    _lastEvalMs = 0;
    _prevP4Requested = false;
    _prevHeating = false;
    _prevInhibited = false;
}

K1Decision K1Logic::update(const K1Inputs& in, const HomeHeatingSettings& g, uint64_t nowMs) {
    K1Decision d = {NO_COMMAND, K1Mode::Unknown, false, 0.0f, false, false, false, 0.0f};
    const uint32_t recalMs = k1RecalMs(g);

    // 1. Motion.
    _est.applyMotion(in.motion, k1TravelMs(g));
    if (_recalibrating) {
        if (in.motion.openMs > 0u) {
            _recalCloseMs = 0;
        } else {
            _recalCloseMs += in.motion.closeMs;
        }
    }

    // 2. Recal triggers.
    bool closeIssued = false;
    const bool trigger = !_booted || (_prevP4Requested && !in.p4Requested) || (_prevHeating && !in.heatingEnabled);
    _booted = true;
    if (trigger && !_recalibrating) {
        _recalibrating = true;
        _recalCloseMs = 0;
        d.recalStarted = true;
        if (!in.inhibited && !in.k1AntiSeizeOwned) {
            d.cmd = K1Command{true, K1Direction::Close, recalMs};
            closeIssued = true;
        }
    }

    // 3. Movement permission; inhibit end re-arms the feed-forward.
    const bool canMove = !in.k1Busy && !in.inhibited && !in.k1AntiSeizeOwned;
    if (_prevInhibited && !in.inhibited) {
        _ffApplied = false;
        _needEval = true;
    }

    bool runControl = true;
    // 4. Recalibration.
    if (_recalibrating) {
        if (_recalCloseMs + 1000u > recalMs) {
            _est.setKnown(0.0f);
            _recalibrating = false;
            d.recalEnded = true;
        } else {
            _mode = K1Mode::Recalibrating;
            if (canMove && !closeIssued) {
                d.cmd = K1Command{true, K1Direction::Close, recalMs - _recalCloseMs};
            }
            runControl = false;
        }
    }

    if (runControl) {
        // 5. Mode selection.
        const K1Mode mode = selectMode(in);
        if (mode != _mode) {
            _mode = mode;
            _ffApplied = false;
            _anchorApplied = false;
            _needEval = true;
        }

        // 6. Actions.
        if (canMove) {
            const float from = _est.pct();
            switch (_mode) {
                case K1Mode::Closed:
                    d.cmd = toCommand(planMoveTo(from, 0.0f, g));
                    break;
                case K1Mode::FailPosFixed:
                    d.cmd = toCommand(planMoveTo(from, static_cast<float>(g.k1FailPosPct), g));
                    break;
                case K1Mode::FailPosFeedback:
                    if (!_anchorApplied) {
                        d.cmd = toCommand(planMoveTo(from, static_cast<float>(g.k1FailPosPct), g));
                        _anchorApplied = true;
                        _lastEvalMs = nowMs;
                        _needEval = false;
                        break;
                    }
                    // fall through to the periodic evaluation
                case K1Mode::Normal:
                case K1Mode::FeedbackOnly:
                case K1Mode::FeedforwardOnly: {
                    const uint64_t periodMs = static_cast<uint64_t>(g.k1PeriodS) * 1000ULL;
                    if (!_needEval && nowMs - _lastEvalMs < periodMs) break;
                    _needEval = false;
                    _lastEvalMs = nowMs;
                    if (_mode == K1Mode::Normal || _mode == K1Mode::FeedforwardOnly) {
                        const float x = feedforwardTarget(in.h1C, g.h2Set, in.h3C, g.k1SmallDiff);
                        const float dx = x > _xApplied ? x - _xApplied : _xApplied - x;
                        if (!_ffApplied || dx >= static_cast<float>(g.k1FfStepPct)) {
                            d.cmd = toCommand(planMoveTo(from, x, g));
                            _ffApplied = true;
                            _xApplied = x;
                            break;
                        }
                        if (_mode == K1Mode::FeedforwardOnly) break;
                    }
                    // C7 step 6 / D4: H2 not Ok -> skip the feedback move, the period is still consumed.
                    if (_mode == K1Mode::FailPosFeedback && in.h2 != SensorHealth::Ok) break;
                    d.cmd = toCommand(planFeedback(from, in.h2C, g.h2Set, g));
                    break;
                }
                default:   // Wait: hold position
                    break;
            }
        }
    }

    // 7. Outputs and edges.
    d.mode = _mode;
    d.known = _est.known();
    d.posPct = _est.pct();
    d.ffValid = _ffApplied;
    d.ffAppliedPct = _xApplied;
    _prevP4Requested = in.p4Requested;
    _prevHeating = in.heatingEnabled;
    _prevInhibited = in.inhibited;
    return d;
}
