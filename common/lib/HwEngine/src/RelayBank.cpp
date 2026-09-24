#include "RelayBank.h"

RelayBank::Slot* RelayBank::slotFor(uint8_t channel) {
    if (channel >= RELAY_CHANNEL_COUNT) {
        return nullptr;
    }
    return &_slots[channel];
}

const RelayBank::Slot* RelayBank::slotFor(uint8_t channel) const {
    if (channel >= RELAY_CHANNEL_COUNT) {
        return nullptr;
    }
    return &_slots[channel];
}

RelayBank::Effective RelayBank::effectiveOf(const Slot& s) {
    if (s.safetySet) {
        return {s.safety, RelayReason::Safety};
    }
    if (s.exerciseSet) {
        return {s.exercise, RelayReason::AntiSeize};
    }
    return {s.control, s.controlReason};
}

void RelayBank::configure(const RelayChannelDesc* channels, size_t count, uint64_t bootMs) {
    for (uint8_t ch = 0; ch < RELAY_CHANNEL_COUNT; ++ch) {
        _slots[ch] = Slot{};
    }
    for (size_t i = 0; i < count; ++i) {
        const RelayChannelDesc& d = channels[i];
        if (d.channel >= RELAY_CHANNEL_COUNT) {
            continue;
        }
        Slot& s = _slots[d.channel];
        s = Slot{};
        s.configured = true;
        s.lockable = d.lockable;
        s.logChanges = d.logChanges;
        // Boot counts as an OFF transition (D6): the lock window starts at
        // bootMs, so a lockable channel cannot switch ON in under lockMs after
        // a (watchdog) reboot.
        s.lastChangeMs = bootMs;
        s.lastOnMs = bootMs;
        s.lastReason = RelayReason::Boot;
    }
}

bool RelayBank::requestControl(uint8_t channel, bool on, RelayReason reason) {
    Slot* s = slotFor(channel);
    if (s == nullptr || !s->configured) {
        return false;
    }
    s->control = on;
    s->controlReason = reason;
    return true;
}

bool RelayBank::requestSafety(uint8_t channel, bool on) {
    Slot* s = slotFor(channel);
    if (s == nullptr || !s->configured) {
        return false;
    }
    s->safetySet = true;
    s->safety = on;
    return true;
}

void RelayBank::clearSafety(uint8_t channel) {
    Slot* s = slotFor(channel);
    if (s == nullptr) {
        return;
    }
    s->safetySet = false;
}

bool RelayBank::setExercise(uint8_t channel, std::optional<bool> on) {
    Slot* s = slotFor(channel);
    if (s == nullptr || !s->configured) {
        return false;
    }
    if (on.has_value()) {
        s->exerciseSet = true;
        s->exercise = *on;
    } else {
        s->exerciseSet = false;
    }
    return true;
}

void RelayBank::update(uint64_t nowMs, EventSink& events) {
    for (uint8_t ch = 0; ch < RELAY_CHANNEL_COUNT; ++ch) {
        Slot& s = _slots[ch];
        if (!s.configured) {
            continue;
        }

        const Effective eff = effectiveOf(s);

        if (s.actual) {
            s.lastOnMs = nowMs;
        }

        if (eff.on == s.actual) {
            // Not currently mismatched: no delay in effect. delayLogged is left
            // untouched (D9: it only resets on an actual switch).
            s.lockDelayed = false;
            continue;
        }

        const uint64_t elapsedSinceChange = nowMs - s.lastChangeMs;
        const bool bypass =
            !s.lockable || eff.source == RelayReason::Safety || _lockMs == 0 || elapsedSinceChange >= _lockMs;

        if (bypass) {
            const bool wasOn = s.actual;
            s.actual = eff.on;
            s.lastChangeMs = nowMs;
            s.hasChanged = true;
            s.lastReason = eff.source;
            s.delayLogged = false;
            s.lockDelayed = false;

            if (s.actual) {
                s.hasBeenOn = true;
                s.lastOnMs = nowMs;
            } else if (wasOn) {
                s.lastOnMs = nowMs;
            }

            if (s.logChanges) {
                events.logEvent(toU16(EventType::RelayChanged), static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + ch),
                    s.actual ? 1.0f : 0.0f, static_cast<float>(static_cast<uint8_t>(eff.source)), EventReason::Logic);
            }
        } else {
            s.lockDelayed = true;
            if (!s.delayLogged) {
                const uint64_t remainingMs = elapsedSinceChange < _lockMs ? _lockMs - elapsedSinceChange : 0;
                const uint32_t remainingS = static_cast<uint32_t>((remainingMs + 999) / 1000);
                events.logEvent(toU16(EventType::RelayLockDelay), static_cast<uint16_t>(EVENT_SOURCE_RELAY_BASE + ch),
                    eff.on ? 1.0f : 0.0f, static_cast<float>(remainingS), EventReason::Logic);
                s.delayLogged = true;
            }
        }
    }
}

bool RelayBank::configured(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr && s->configured;
}

bool RelayBank::actual(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr && s->actual;
}

bool RelayBank::requested(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    if (s == nullptr || !s->configured) {
        return false;
    }
    return effectiveOf(*s).on;
}

bool RelayBank::safetyActive(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr && s->safetySet;
}

bool RelayBank::lockDelayed(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr && s->lockDelayed;
}

uint32_t RelayBank::lockRemainingMs(uint8_t channel, uint64_t nowMs) const {
    const Slot* s = slotFor(channel);
    if (s == nullptr || !s->lockDelayed) {
        return 0;
    }
    const uint64_t elapsed = nowMs - s->lastChangeMs;
    if (elapsed >= _lockMs) {
        return 0;
    }
    return static_cast<uint32_t>(_lockMs - elapsed);
}

bool RelayBank::hasBeenOn(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr && s->hasBeenOn;
}

uint64_t RelayBank::lastOnMs(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr ? s->lastOnMs : 0;
}

bool RelayBank::hasChanged(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr && s->hasChanged;
}

uint64_t RelayBank::lastChangeMs(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr ? s->lastChangeMs : 0;
}

RelayReason RelayBank::lastReason(uint8_t channel) const {
    const Slot* s = slotFor(channel);
    return s != nullptr ? s->lastReason : RelayReason::None;
}

uint8_t RelayBank::outputByte(bool activeLow) const {
    uint8_t bits = 0;
    for (uint8_t ch = 0; ch < RELAY_CHANNEL_COUNT; ++ch) {
        if (_slots[ch].configured && _slots[ch].actual) {
            bits |= static_cast<uint8_t>(1u << ch);
        }
    }
    return activeLow ? static_cast<uint8_t>(~bits) : bits;
}

void RelayBank::fillStatus(RelayArray& out, uint64_t nowMs) const {
    for (uint8_t ch = 0; ch < RELAY_CHANNEL_COUNT; ++ch) {
        const Slot& s = _slots[ch];
        out.on[ch] = s.actual;

        RelayChannelStatus& cs = out.channel[ch];
        cs.requested = requested(ch);
        cs.safety = s.safetySet;
        cs.lockDelayed = s.lockDelayed;
        const uint32_t remMs = lockRemainingMs(ch, nowMs);
        cs.lockRemainingS = static_cast<uint16_t>((remMs + 999) / 1000);
        cs.reason = s.lastReason;
    }
}
