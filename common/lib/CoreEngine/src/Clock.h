#pragma once
#include <stdint.h>

// UTC epoch seconds if realTime is true, otherwise seconds since boot (uptime).
struct Timestamp {
    uint32_t seconds;
    bool realTime;
};

// Time source interface. CoreEsp32's TimeService implements this over the RTC/SNTP;
// common/test/fakes/FakeClock.h implements it for native tests.
class Clock {
public:
    virtual ~Clock() = default;

    // Monotonic milliseconds, never wraps within device lifetime (uint64_t).
    virtual uint64_t monoMs() const = 0;

    // Current wall-clock/uptime timestamp; see Timestamp for meaning of realTime.
    virtual Timestamp now() const = 0;
};
