#pragma once
#include "../../lib/CoreEngine/src/Clock.h"

// Header-only settable Clock fake for native tests.
class FakeClock : public Clock {
public:
    uint64_t mono = 0;   // monotonic milliseconds
    uint32_t utc = 0;    // seconds: UTC epoch if realTime, else uptime
    bool realTime = false;

    uint64_t monoMs() const override { return mono; }
    Timestamp now() const override { return Timestamp{utc, realTime}; }

    // Advances mono by ms; also advances utc by the same elapsed seconds when realTime.
    void advance(uint64_t ms) {
        mono += ms;
        if (realTime) {
            utc += static_cast<uint32_t>(ms / 1000);
        }
    }
};
