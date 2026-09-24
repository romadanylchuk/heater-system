#include "SettingClamp.h"

#include <math.h>
#include <string.h>

ClampResult clampNumber(const SettingDescriptor& d, float requested) {
    if (!isfinite(requested)) {
        return ClampResult{ClampOutcome::Rejected, requested};
    }

    if (d.type == SettingType::Bool) {
        return ClampResult{ClampOutcome::InRange, requested != 0.0f ? 1.0f : 0.0f};
    }

    float value = requested;
    if (d.type == SettingType::Int) {
        // Limit to [min-1, max+1] in float first so lroundf never sees a value outside
        // `long` range (UB/unspecified, e.g. 1e10 -> 0 on mingw). The one-step margin
        // keeps "rounding alone != Clamped" (100.3 with max 100 still rounds to 100,
        // InRange); anything further out still ends up Clamped by the checks below.
        value = fminf(fmaxf(value, d.minValue - 1.0f), d.maxValue + 1.0f);
        value = static_cast<float>(lroundf(value));
    }

    ClampOutcome outcome = ClampOutcome::InRange;
    if (value < d.minValue) {
        value = d.minValue;
        outcome = ClampOutcome::Clamped;
    } else if (value > d.maxValue) {
        value = d.maxValue;
        outcome = ClampOutcome::Clamped;
    }

    return ClampResult{outcome, value};
}

bool isTextLengthValid(const SettingDescriptor& d, const char* text) {
    if (text == nullptr) {
        return false;
    }
    size_t len = strlen(text);
    return len >= static_cast<size_t>(d.minValue) && len <= static_cast<size_t>(d.maxLen);
}
