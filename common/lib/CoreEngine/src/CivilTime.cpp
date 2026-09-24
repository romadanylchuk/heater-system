#include "CivilTime.h"

namespace {

// Howard Hinnant's days-from-civil: days since 1970-01-01 for a proleptic
// Gregorian civil date (m: 1..12, d: 1..31). See
// http://howardhinnant.github.io/date_algorithms.html
int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
    y -= (m <= 2) ? 1 : 0;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = static_cast<uint32_t>(y - era * 400);                     // [0, 399]
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;            // [0, 365]
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                     // [0, 146096]
    return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

// Inverse of daysFromCivil: civil date from days since 1970-01-01.
void civilFromDays(int32_t z, int32_t& yOut, uint32_t& mOut, uint32_t& dOut) {
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = static_cast<uint32_t>(z - era * 146097);                  // [0, 146096]
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;    // [0, 399]
    int32_t y = static_cast<int32_t>(yoe) + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                  // [0, 365]
    uint32_t mp = (5 * doy + 2) / 153;                                       // [0, 11]
    dOut = doy - (153 * mp + 2) / 5 + 1;                                     // [1, 31]
    mOut = mp + (mp < 10 ? 3 : -9);                                          // [1, 12]
    yOut = y + (mOut <= 2 ? 1 : 0);
}

bool isLeapYear(int32_t y) { return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0)); }

uint8_t daysInMonth(int32_t y, uint32_t m) {
    static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && isLeapYear(y)) {
        return 29;
    }
    return kDays[m - 1];
}

}  // namespace

bool isValidCivil(const CivilDateTime& c) {
    if (c.year < 1970 || c.year > 2105) {
        return false;
    }
    if (c.month < 1 || c.month > 12) {
        return false;
    }
    if (c.day < 1 || c.day > daysInMonth(c.year, c.month)) {
        return false;
    }
    if (c.hour > 23 || c.minute > 59 || c.second > 59) {
        return false;
    }
    return true;
}

uint32_t civilToEpoch(const CivilDateTime& c) {
    int32_t days = daysFromCivil(c.year, c.month, c.day);
    int64_t seconds = static_cast<int64_t>(days) * 86400 + c.hour * 3600 + c.minute * 60 + c.second;
    return static_cast<uint32_t>(seconds);
}

CivilDateTime epochToCivil(uint32_t epoch) {
    int32_t days = static_cast<int32_t>(epoch / 86400);
    uint32_t rem = epoch % 86400;

    int32_t y = 0;
    uint32_t m = 0, d = 0;
    civilFromDays(days, y, m, d);

    CivilDateTime c{};
    c.year = static_cast<uint16_t>(y);
    c.month = static_cast<uint8_t>(m);
    c.day = static_cast<uint8_t>(d);
    c.hour = static_cast<uint8_t>(rem / 3600);
    c.minute = static_cast<uint8_t>((rem % 3600) / 60);
    c.second = static_cast<uint8_t>(rem % 60);

    // 1970-01-01 (days == 0) was a Thursday. weekday_from_days below (Hinnant)
    // yields 0=Sunday..6=Saturday; remap to 1=Monday..7=Sunday.
    int32_t wd0 = (days >= -4) ? (days + 4) % 7 : (days + 5) % 7 + 6;
    c.weekday = static_cast<uint8_t>(wd0 == 0 ? 7 : wd0);
    return c;
}
