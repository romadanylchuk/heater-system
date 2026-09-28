#pragma once
#include <stdint.h>
#include <LocalTime.h>

// K1 motor-run counter per local day (stage 09, C12, A9, D9): counts the
// deltas of K1Driver::runStarts() into "today" and rolls today -> yesterday at
// the local-day change. Pure, no clock of its own: the local day is injected.
class K1PulseCounter {
public:
    // today 0, yesterday invalid (0), day unknown, lastStarts = baselineStarts (the
    // driver's current runStarts(), so a re-begin does not count earlier runs as today).
    void reset(uint32_t baselineStarts = 0);

    // delta = runStarts - lastStarts (unsigned, so a wrap counts correctly). Then:
    //  - local.valid && !dayKnown -> day = local.dayIndex (no rollover: the
    //    counts from before the first valid time stay "today", A9).
    //  - local.valid && local.dayIndex != day -> yesterday = (dayIndex == day + 1)
    //    ? today : 0 (D9: a multi-day gap or a backward step gives 0);
    //    yesterdayValid = true; today = 0; day = local.dayIndex.
    //  - then today += delta.
    //  - invalid time -> no rollover, keep accumulating.
    void update(uint32_t runStarts, const LocalTimeInfo& local);

    uint32_t today() const { return _today; }
    bool yesterdayValid() const { return _yesterdayValid; }
    uint32_t yesterday() const { return _yesterday; }

private:
    uint32_t _today = 0;
    uint32_t _yesterday = 0;
    bool _yesterdayValid = false;
    bool _dayKnown = false;
    uint32_t _day = 0;
    uint32_t _lastStarts = 0;
};
