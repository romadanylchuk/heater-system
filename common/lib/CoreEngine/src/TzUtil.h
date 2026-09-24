#pragma once
#include <stddef.h>

// Pure plausibility check for a POSIX TZ string (D16). This is not a full POSIX
// TZ parser: it only rejects obviously-wrong input before it reaches
// setenv("TZ")/tzset() in TimeService (CoreEsp32). DST correctness itself is
// left to newlib's TZ rules and is not native-tested (mingw's libc does not
// implement them).
constexpr size_t TZ_MAX_LEN = 48;

// Non-null, 1..TZ_MAX_LEN chars; starts with either >= 3 letters or a
// <...>-quoted name; then an optional sign and at least one digit (the UTC
// offset); the remainder must be printable ASCII.
bool isPlausiblePosixTz(const char* tz);

// configured if plausible, else DEFAULT_TZ (CommonSettings.h).
const char* effectiveTz(const char* configured);
