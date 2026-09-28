#include "HomeHeatingDiagSettings.h"
#include <string.h>

namespace {

// Default of the row with the given key; the key set is fixed at compile time
// and covered by tests, so a miss (0) can only be a programming error.
float defaultOf(const char* key) {
    for (size_t i = 0; i < HOME_HEATING_DIAG_SETTING_COUNT; ++i) {
        if (strcmp(HOME_HEATING_DIAG_SETTINGS[i].key, key) == 0) {
            return HOME_HEATING_DIAG_SETTINGS[i].defaultValue;
        }
    }
    return 0.0f;
}

uint32_t defaultU32(const char* key) {
    float v = defaultOf(key);
    return v <= 0.0f ? 0u : static_cast<uint32_t>(v);
}

}  // namespace

HomeHeatingDiagSettings defaultHomeHeatingDiagSettings() {
    HomeHeatingDiagSettings s{};
    s.h1.enabled = defaultOf(HH_KEY_H1_EN) != 0.0f;
    s.h1.minOnMin = defaultU32(HH_KEY_H1_MIN_ON);
    s.h1.k1MinPct = defaultU32(HH_KEY_H1_K1_MIN);
    s.h1.deltaC = defaultOf(HH_KEY_H1_DELTA);
    s.h1.minDiffC = defaultOf(HH_KEY_H1_MIN_DIFF);
    s.k1StepPulseS = defaultU32(HH_KEY_K1_STEP_PULSE);
    return s;
}
