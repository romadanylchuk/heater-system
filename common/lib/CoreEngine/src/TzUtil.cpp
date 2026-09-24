#include "TzUtil.h"

#include <string.h>

#include "CommonSettings.h"

namespace {

bool isAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool isPrintableAscii(char c) { return c >= 0x20 && c <= 0x7E; }

}  // namespace

bool isPlausiblePosixTz(const char* tz) {
    if (tz == nullptr) {
        return false;
    }
    size_t len = strlen(tz);
    if (len < 1 || len > TZ_MAX_LEN) {
        return false;
    }

    size_t i = 0;
    if (tz[0] == '<') {
        size_t close = 1;
        while (close < len && tz[close] != '>') {
            ++close;
        }
        if (close >= len || close == 1) {  // no closing '>', or an empty <>
            return false;
        }
        i = close + 1;
    } else {
        size_t start = i;
        while (i < len && isAsciiAlpha(tz[i])) {
            ++i;
        }
        if (i - start < 3) {
            return false;
        }
    }

    if (i < len && (tz[i] == '+' || tz[i] == '-')) {
        ++i;
    }
    if (i >= len || !isAsciiDigit(tz[i])) {
        return false;
    }
    ++i;

    for (; i < len; ++i) {
        if (!isPrintableAscii(tz[i])) {
            return false;
        }
    }
    return true;
}

const char* effectiveTz(const char* configured) {
    return isPlausiblePosixTz(configured) ? configured : DEFAULT_TZ;
}
