#pragma once
#include <stddef.h>
#include <stdint.h>

// Fixed-capacity per (type, source) rate limiter: a key accepted within WINDOW_MS
// of its last acceptance is suppressed. Uses only the injected monotonic clock, so
// it works from boot even before real time is known.
class EventRateLimiter {
public:
    static constexpr uint32_t WINDOW_MS = 60000;
    static constexpr size_t SLOTS = 32;

    // True if accepted (and recorded as the new "last accepted" for this key).
    bool allow(uint16_t type, uint16_t source, uint64_t monoMs);

    void reset();

private:
    struct Slot {
        bool used = false;
        uint16_t type = 0;
        uint16_t source = 0;
        uint64_t lastAcceptedMs = 0;
    };

    Slot _slots[SLOTS];
};
