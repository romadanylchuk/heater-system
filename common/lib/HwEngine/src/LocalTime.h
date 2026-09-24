#pragma once
#include <stdint.h>
#include <CivilTime.h>

// Local-day/time info for AntiSeizeScheduler's daily checkpoint (D19, D21).
// Pure: HardwareServices builds this from localtime_r (already TZ-applied by
// TimeService) only when the RTC/NTP time is valid (state.time.valid);
// otherwise it passes NO_LOCAL_TIME and the scheduler falls back to the
// uptime rule. DST correctness itself remains newlib's (stage-02 D16); this
// struct only carries the already-resolved local wall-clock fields.
struct LocalTimeInfo {
    bool valid;
    uint32_t dayIndex;     // days since 1970-01-01 of the LOCAL date
    uint16_t minuteOfDay;  // 0..1439
};

// Builds a valid LocalTimeInfo from a local civil date/time (already
// validated/TZ-applied by the caller). dayIndex = civilToEpoch(date at
// 00:00:00) / 86400; minuteOfDay = local.hour * 60 + local.minute.
LocalTimeInfo makeLocalTimeInfo(const CivilDateTime& local);

constexpr LocalTimeInfo NO_LOCAL_TIME = {false, 0, 0};
