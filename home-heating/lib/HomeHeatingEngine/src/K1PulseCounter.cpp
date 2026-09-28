#include "K1PulseCounter.h"

void K1PulseCounter::reset(uint32_t baselineStarts) {
    _today = 0;
    _yesterday = 0;
    _yesterdayValid = false;
    _dayKnown = false;
    _day = 0;
    _lastStarts = baselineStarts;
}

void K1PulseCounter::update(uint32_t runStarts, const LocalTimeInfo& local) {
    const uint32_t delta = runStarts - _lastStarts;  // unsigned: wrap-safe
    _lastStarts = runStarts;
    if (local.valid) {
        if (!_dayKnown) {
            _dayKnown = true;
            _day = local.dayIndex;
        } else if (local.dayIndex != _day) {
            _yesterday = (local.dayIndex == _day + 1u) ? _today : 0u;
            _yesterdayValid = true;
            _today = 0;
            _day = local.dayIndex;
        }
    }
    _today += delta;
}
