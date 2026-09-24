#pragma once
#include "SettingDescriptor.h"

// Pure numeric/text validation used by ConfigEngine (boot load + runtime set). No
// hardware/storage dependency; native-tested directly.
enum class ClampOutcome : uint8_t { InRange, Clamped, Rejected };

struct ClampResult {
    ClampOutcome outcome;
    float value;
};

// NaN/inf -> Rejected (value echoes the requested value, callers must not persist it).
// Int is pre-limited to [min-1, max+1] in float (keeps lroundf in `long` range for
// huge requests like 1e10), then rounded (rounding alone does not count as Clamped).
// Bool coerces to 0/1 and is always InRange. Float/Int are clamped to [min, max];
// Clamped is reported only when the limits actually changed the value.
ClampResult clampNumber(const SettingDescriptor& d, float requested);

// Text: non-null and minValue <= strlen(text) <= maxLen.
bool isTextLengthValid(const SettingDescriptor& d, const char* text);
