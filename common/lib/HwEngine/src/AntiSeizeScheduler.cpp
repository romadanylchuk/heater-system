#include "AntiSeizeScheduler.h"
#include <optional>

AntiSeizeScheduler::AntiSeizeScheduler(RelayBank& relays, K1Driver& k1, EventSink& events)
    : _relays(relays), _k1(k1), _events(events) {}

void AntiSeizeScheduler::configure(const AntiSeizeOutputDesc* outputs, size_t count, uint64_t bootMs) {
    const size_t n = count > MAX_ANTI_SEIZE_OUTPUTS ? MAX_ANTI_SEIZE_OUTPUTS : count;
    for (size_t i = 0; i < MAX_ANTI_SEIZE_OUTPUTS; ++i) {
        _outputs[i] = Output{};
    }
    for (size_t i = 0; i < n; ++i) {
        _outputs[i].desc = outputs[i];
        _outputs[i].configured = true;
        // Boot counts as "just ran" (mirrors RelayBank's D6 boot-lock
        // convention), so a freshly booted controller does not immediately
        // treat every output as overdue.
        _outputs[i].lastRun = bootMs;
    }
    _count = n;
    _settings = AntiSeizeSettings{};
    _strokeMs = K1_DEFAULT_STROKE_MS;
    _lastCheckDay = 0;
}

void AntiSeizeScheduler::setSettings(const AntiSeizeSettings& settings) { _settings = settings; }

void AntiSeizeScheduler::setK1StrokeMs(uint32_t strokeMs) { _strokeMs = strokeMs; }

void AntiSeizeScheduler::setInhibited(size_t output, bool inhibited) {
    if (output < _count) {
        _outputs[output].inhibited = inhibited;
    }
}

uint64_t AntiSeizeScheduler::lastRunMs(size_t output) const { return output < _count ? _outputs[output].lastRun : 0; }

bool AntiSeizeScheduler::pending(size_t output) const { return output < _count && _outputs[output].pending; }

bool AntiSeizeScheduler::running(size_t output) const { return output < _count && _outputs[output].running; }

uint64_t AntiSeizeScheduler::activityOf(const AntiSeizeOutputDesc& desc) const {
    switch (desc.kind) {
        case AntiSeizeKind::Pump:
            return _relays.lastOnMs(desc.channel);
        case AntiSeizeKind::Toggle:
            return _relays.lastChangeMs(desc.channel);
        case AntiSeizeKind::ValveStroke:
            return _k1.hasMoved() ? _k1.lastMoveMs() : 0;
    }
    return 0;
}

bool AntiSeizeScheduler::isBlocked(const Output& out) const {
    if (out.inhibited) {
        return true;
    }
    if (_relays.safetyActive(out.desc.channel)) {
        return true;
    }
    if (out.desc.blockWhileOnChannel != NO_RELAY_CHANNEL && _relays.actual(out.desc.blockWhileOnChannel)) {
        return true;
    }
    if (out.desc.kind == AntiSeizeKind::ValveStroke) {
        // A controller pulse counts as movement anyway, so there is nothing
        // useful for us to do while K1 is already busy.
        return _k1.busy();
    }
    // A Pump/Toggle never switches its channel ON while a ValveStroke output
    // that is blocked by it is actually running (so an exercise never sends
    // uncontrolled temperature to the radiators mid-stroke).
    for (size_t j = 0; j < _count; ++j) {
        const Output& other = _outputs[j];
        if (other.desc.kind == AntiSeizeKind::ValveStroke && other.running &&
            other.desc.blockWhileOnChannel == out.desc.channel) {
            return true;
        }
    }
    return false;
}

void AntiSeizeScheduler::startOutput(Output& out, size_t idx, uint64_t nowMs) {
    (void)idx;
    out.pending = false;
    out.running = true;
    out.runStartMs = nowMs;

    float plannedS = static_cast<float>(_settings.durationS);
    switch (out.desc.kind) {
        case AntiSeizeKind::Pump:
            _relays.setExercise(out.desc.channel, true);
            out.phase = Phase::WaitOn;
            break;
        case AntiSeizeKind::Toggle:
            out.toggleTarget = !_relays.actual(out.desc.channel);
            _relays.setExercise(out.desc.channel, out.toggleTarget);
            out.phase = Phase::WaitOn;
            break;
        case AntiSeizeKind::ValveStroke:
            if (!_k1.requestPulse(K1Direction::Open, _strokeMs, nowMs, K1Owner::AntiSeize)) {
                // A misconfigured stroke duration (0 or > K1Driver::MAX_PULSE_MS)
                // would otherwise silently "run" forever without ever moving
                // anything; skip this cycle (fail-safe) and retry at the next
                // trigger instead.
                out.running = false;
                return;
            }
            out.phase = Phase::K1Open;
            // The whole stroke (open leg + close leg) is the planned run.
            plannedS = static_cast<float>((2ull * _strokeMs + 999) / 1000);
            break;
    }

    // AntiSeizeRun is logged once when a run starts (D8/D20): source is the
    // relay channel that was acted on (the K1 power channel for ValveStroke,
    // which is exactly out.desc.channel per the HwConfig contract).
    _events.logEvent(toU16(EventType::AntiSeizeRun), static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + out.desc.channel),
        plannedS, static_cast<float>(static_cast<uint8_t>(out.desc.kind)), EventReason::Logic);
}

void AntiSeizeScheduler::advanceValveStroke(Output& out, size_t idx, uint64_t nowMs) {
    const bool busyNow = _k1.busy();
    const bool ownedByUs = _k1.owner() == K1Owner::AntiSeize;

    if (busyNow && !ownedByUs) {
        // A controller request replaced ours (K1 owner changed): release our
        // own tracking without touching the driver -- it is not ours to
        // cancel any more (D20).
        out.running = false;
        out.phase = Phase::None;
        return;
    }

    const bool blockedOn =
        out.desc.blockWhileOnChannel != NO_RELAY_CHANNEL && _relays.actual(out.desc.blockWhileOnChannel);
    const bool safetyAppeared = _relays.safetyActive(out.desc.channel);
    if (!_settings.enabled[idx] || out.inhibited || blockedOn || safetyAppeared) {
        if (busyNow && ownedByUs) {
            _k1.cancel(nowMs);
        }
        out.running = false;
        out.phase = Phase::None;
        return;
    }

    if (busyNow) {
        return;  // current leg still pending/running
    }

    // The current leg finished naturally (K1Driver::tick() completed it).
    if (out.phase == Phase::K1Open) {
        if (!_k1.requestPulse(K1Direction::Close, _strokeMs, nowMs, K1Owner::AntiSeize)) {
            // The stroke duration became invalid mid-stroke (setK1StrokeMs()
            // to 0 or > K1Driver::MAX_PULSE_MS): the close leg never starts,
            // so the run must not be treated as a completed stroke. Abort it
            // and report a diagnostic (mirrors the open-leg guard in
            // startOutput()).
            out.running = false;
            out.phase = Phase::None;
            _events.logEvent(toU16(EventType::DiagnosticWarning),
                static_cast<uint16_t>(EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ANTISEIZE_K1_STROKE),
                static_cast<float>(_strokeMs), 0.0f, EventReason::Logic);
            return;
        }
        out.phase = Phase::K1Close;
    } else {
        // Close leg done: the whole stroke completed, ending idle/closed.
        out.running = false;
        out.phase = Phase::None;
    }
}

void AntiSeizeScheduler::advanceRunning(Output& out, size_t idx, uint64_t nowMs) {
    if (!out.running) {
        return;
    }
    if (out.desc.kind == AntiSeizeKind::ValveStroke) {
        advanceValveStroke(out, idx, nowMs);
        return;
    }

    const bool disabled = !_settings.enabled[idx];
    const bool safetyAppeared = _relays.safetyActive(out.desc.channel);
    if (disabled || out.inhibited || safetyAppeared) {
        _relays.setExercise(out.desc.channel, std::nullopt);
        out.running = false;
        out.phase = Phase::None;
        return;
    }

    // Pump/Toggle timeout: asDuration + 600 s + 60 s after the run started,
    // covering a lock (or a target never reached) that would otherwise hold
    // the exercise slot forever.
    const uint64_t timeoutMs = static_cast<uint64_t>(_settings.durationS) * 1000 + 600000ull + 60000ull;
    if (nowMs - out.runStartMs >= timeoutMs) {
        _relays.setExercise(out.desc.channel, std::nullopt);
        out.running = false;
        out.phase = Phase::None;
        return;
    }

    if (out.phase == Phase::WaitOn) {
        const bool reachedTarget = out.desc.kind == AntiSeizeKind::Pump ? _relays.actual(out.desc.channel)
                                                                         : _relays.actual(out.desc.channel) == out.toggleTarget;
        if (reachedTarget) {
            out.phase = Phase::Hold;
            out.holdStartMs = nowMs;
        }
        return;
    }

    if (out.phase == Phase::Hold) {
        if (nowMs - out.holdStartMs >= static_cast<uint64_t>(_settings.durationS) * 1000) {
            _relays.setExercise(out.desc.channel, std::nullopt);
            out.running = false;
            out.phase = Phase::None;
        }
    }
}

void AntiSeizeScheduler::tick(uint64_t nowMs, const LocalTimeInfo& local) {
    // (1) Refresh lastRun from RelayBank/K1Driver activity: any run, by any
    // actor, counts (D19).
    for (size_t i = 0; i < _count; ++i) {
        const uint64_t activity = activityOf(_outputs[i].desc);
        if (activity > _outputs[i].lastRun) {
            _outputs[i].lastRun = activity;
        }
    }

    // (2) Clear pending once the output has actually run since it went
    // pending, or it was disabled meanwhile (D19: "disabling clears pending").
    for (size_t i = 0; i < _count; ++i) {
        Output& out = _outputs[i];
        if (out.pending && (!_settings.enabled[i] || out.lastRun >= out.pendingSinceMs)) {
            out.pending = false;
        }
    }

    // (3) Evaluate triggers -- checkpoint (clock valid) / uptime (clock
    // invalid) / safety net (always) -- only for enabled, idle (not running,
    // not already pending) outputs (D19).
    bool checkpointNow = false;
    if (local.valid && local.dayIndex > _lastCheckDay && local.minuteOfDay >= _settings.startMinute) {
        checkpointNow = true;
        _lastCheckDay = local.dayIndex;  // dayIndex must strictly increase: at most one checkpoint per day
    }
    const uint64_t intervalMs = static_cast<uint64_t>(_settings.intervalDays) * 86400000ull;
    for (size_t i = 0; i < _count; ++i) {
        Output& out = _outputs[i];
        if (!_settings.enabled[i] || out.running || out.pending) {
            continue;
        }
        const uint64_t idleMs = nowMs - out.lastRun;
        bool trig = false;
        if (checkpointNow && idleMs + CHECK_SLACK_MS >= intervalMs) {
            trig = true;
        }
        if (!local.valid && idleMs >= intervalMs) {
            trig = true;
        }
        if (idleMs >= intervalMs + SAFETY_NET_MS) {
            trig = true;
        }
        if (trig) {
            out.pending = true;
            out.pendingSinceMs = nowMs;
        }
    }

    // (4) Advance running outputs: complete or abort (D20).
    for (size_t i = 0; i < _count; ++i) {
        advanceRunning(_outputs[i], i, nowMs);
    }

    // (5) Start pending, unblocked outputs. Array order matters: an output
    // that starts earlier in this very loop is visible to a later output's
    // "blocked by a running ValveStroke" check (isBlocked), matching the
    // descriptor order both projects declare (pumps/toggle before K1).
    for (size_t i = 0; i < _count; ++i) {
        Output& out = _outputs[i];
        if (!out.pending || out.running || !_settings.enabled[i]) {
            continue;
        }
        if (isBlocked(out)) {
            continue;
        }
        startOutput(out, i, nowMs);
    }
}

void AntiSeizeScheduler::fillStatus(AntiSeizeStatus& out, uint64_t nowMs) const {
    out.count = static_cast<uint8_t>(_count);
    for (size_t i = 0; i < MAX_ANTI_SEIZE_OUTPUTS; ++i) {
        AntiSeizeOutputStatus& s = out.output[i];
        if (i < _count) {
            s.enabled = _settings.enabled[i];
            s.pending = _outputs[i].pending;
            s.running = _outputs[i].running;
            const uint64_t idleMs = nowMs > _outputs[i].lastRun ? nowMs - _outputs[i].lastRun : 0;
            s.idleS = static_cast<uint32_t>(idleMs / 1000);
        } else {
            s = AntiSeizeOutputStatus{};
        }
    }
}
