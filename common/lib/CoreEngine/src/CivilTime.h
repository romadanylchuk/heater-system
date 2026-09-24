#pragma once
#include <stdint.h>

// Pure civil-date <-> UTC-epoch conversion (Howard Hinnant's days-from-civil /
// civil-from-days algorithms). No libc time functions, so it behaves the same
// on the host gcc (native tests) and the ESP32 xtensa gcc. Valid for years
// 1970..2105 (a uint32_t epoch cannot represent past 2106-02-07).
struct CivilDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;  // 1 = Monday .. 7 = Sunday. Ignored by civilToEpoch, filled by epochToCivil.
};

// Range + days-in-month (incl. leap years) check for year/month/day/hour/minute/
// second. Does not check or require weekday to be consistent.
bool isValidCivil(const CivilDateTime& c);

// UTC epoch seconds for a civil date/time. The caller must validate with
// isValidCivil() first; out-of-range fields are not re-checked here.
uint32_t civilToEpoch(const CivilDateTime& c);

// Inverse of civilToEpoch; also fills weekday (1 = Monday .. 7 = Sunday).
CivilDateTime epochToCivil(uint32_t epoch);
