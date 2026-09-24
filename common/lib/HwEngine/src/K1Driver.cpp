#include "K1Driver.h"

void K1Driver::begin(uint64_t nowMs) {
    _powerOn = false;
    _dirOpen = false;
    _lastPowerOffMs = nowMs;
    // The rest direction (Close) counts as already settled: back-date
    // lastDirChangeMs by a full dead time (saturating at 0 near boot) so a
    // CLOSE pulse right after begin() can power ON on its very first tick.
    _lastDirChangeMs = nowMs >= DEAD_TIME_MS ? nowMs - DEAD_TIME_MS : 0;

    _running = false;
    _runDir = K1Direction::Close;
    _runStartMs = 0;
    _runEndMs = 0;
    _owner = K1Owner::None;

    _pending = false;
    _pendDir = K1Direction::Close;
    _pendDurMs = 0;
    _pendOwner = K1Owner::None;

    _motion = K1Motion{0, 0};
    _hasMoved = false;
    _lastMoveMs = 0;
}

bool K1Driver::requestPulse(K1Direction dir, uint32_t durationMs, uint64_t nowMs, K1Owner owner) {
    if (durationMs == 0 || durationMs > MAX_PULSE_MS) {
        return false;
    }

    if (_running) {
        if (_runDir == dir) {
            _runEndMs = nowMs + durationMs;
            _owner = owner;
            return true;
        }
        // Opposite direction while running: stop now (power OFF, motion
        // accounted), then queue the reversal below -- it starts only once the
        // dead time / direction-change interlock in tick() allows it.
        stopRun(nowMs);
    }

    // Not running (either it never was, or it was just stopped above): replace
    // any pending request.
    _pending = true;
    _pendDir = dir;
    _pendDurMs = durationMs;
    _pendOwner = owner;
    return true;
}

void K1Driver::cancel(uint64_t nowMs) {
    if (_running) {
        stopRun(nowMs);
    }
    _pending = false;
}

void K1Driver::stopRun(uint64_t nowMs) {
    const uint32_t ranMs = static_cast<uint32_t>(nowMs - _runStartMs);
    if (_runDir == K1Direction::Open) {
        _motion.openMs += ranMs;
    } else {
        _motion.closeMs += ranMs;
    }
    _hasMoved = true;
    _lastMoveMs = nowMs;

    _powerOn = false;
    _running = false;
    _lastPowerOffMs = nowMs;
}

void K1Driver::tick(uint64_t nowMs) {
    if (_running) {
        _lastMoveMs = nowMs;
        if (nowMs >= _runEndMs) {
            stopRun(nowMs);
        }
    }

    if (_pending && !_running) {
        const bool wantOpen = _pendDir == K1Direction::Open;
        if (_dirOpen != wantOpen) {
            // The direction must change: only while power is OFF (always true
            // here since !_running implies !_powerOn) and only once the dead
            // time since the last power-off has elapsed. Power never turns ON
            // on the same tick as a direction change.
            if (!_powerOn && (nowMs - _lastPowerOffMs) >= DEAD_TIME_MS) {
                _dirOpen = wantOpen;
                _lastDirChangeMs = nowMs;
            }
            return;
        }
        if ((nowMs - _lastDirChangeMs) >= DEAD_TIME_MS) {
            _powerOn = true;
            _running = true;
            _runDir = _pendDir;
            _runStartMs = nowMs;
            _runEndMs = nowMs + _pendDurMs;
            _owner = _pendOwner;
            _pending = false;
            _hasMoved = true;
            _lastMoveMs = nowMs;
        }
        return;
    }

    if (!_pending && !_running && _dirOpen && (nowMs - _lastPowerOffMs) >= DEAD_TIME_MS) {
        // Idle rest: after a pulse, power OFF, then >= 1 dead time later the
        // direction is de-energised (CLOSE).
        _dirOpen = false;
        _lastDirChangeMs = nowMs;
    }
}

K1Direction K1Driver::direction() const {
    if (_running) {
        return _runDir;
    }
    if (_pending) {
        return _pendDir;
    }
    return K1Direction::Close;
}

K1Owner K1Driver::owner() const {
    if (_running) {
        return _owner;
    }
    if (_pending) {
        return _pendOwner;
    }
    return K1Owner::None;
}

uint32_t K1Driver::currentRunMs(uint64_t nowMs) const {
    return _running ? static_cast<uint32_t>(nowMs - _runStartMs) : 0;
}

K1Motion K1Driver::takeMotion() {
    const K1Motion m = _motion;
    _motion = K1Motion{0, 0};
    return m;
}
