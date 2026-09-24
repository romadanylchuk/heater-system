#include "EventRateLimiter.h"

bool EventRateLimiter::allow(uint16_t type, uint16_t source, uint64_t monoMs) {
    for (size_t i = 0; i < SLOTS; ++i) {
        Slot& s = _slots[i];
        if (s.used && s.type == type && s.source == source) {
            if (monoMs - s.lastAcceptedMs < WINDOW_MS) {
                return false;
            }
            s.lastAcceptedMs = monoMs;
            return true;
        }
    }

    // New key: prefer a free slot, else an expired one, else evict the oldest.
    int freeIndex = -1;
    int expiredIndex = -1;
    int oldestIndex = 0;
    uint64_t oldestMs = _slots[0].lastAcceptedMs;

    for (size_t i = 0; i < SLOTS; ++i) {
        const Slot& s = _slots[i];
        if (!s.used) {
            freeIndex = static_cast<int>(i);
            break;
        }
        if (expiredIndex < 0 && (monoMs - s.lastAcceptedMs) >= WINDOW_MS) {
            expiredIndex = static_cast<int>(i);
        }
        if (s.lastAcceptedMs < oldestMs) {
            oldestMs = s.lastAcceptedMs;
            oldestIndex = static_cast<int>(i);
        }
    }

    int target = freeIndex >= 0 ? freeIndex : (expiredIndex >= 0 ? expiredIndex : oldestIndex);
    Slot& s = _slots[target];
    s.used = true;
    s.type = type;
    s.source = source;
    s.lastAcceptedMs = monoMs;
    return true;
}

void EventRateLimiter::reset() {
    for (size_t i = 0; i < SLOTS; ++i) {
        _slots[i] = Slot();
    }
}
