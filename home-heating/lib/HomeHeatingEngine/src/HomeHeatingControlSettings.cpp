#include "HomeHeatingControlSettings.h"
#include <string.h>

namespace {

// Default of the row with the given key; the key set is fixed at compile time
// and covered by tests, so a miss (0) can only be a programming error.
float defaultOf(const char* key) {
    for (size_t i = 0; i < HOME_HEATING_CONTROL_SETTING_COUNT; ++i) {
        if (strcmp(HOME_HEATING_CONTROL_SETTINGS[i].key, key) == 0) {
            return HOME_HEATING_CONTROL_SETTINGS[i].defaultValue;
        }
    }
    return 0.0f;
}

uint32_t defaultU32(const char* key) {
    float v = defaultOf(key);
    return v <= 0.0f ? 0u : static_cast<uint32_t>(v);
}

}  // namespace

HomeHeatingSettings defaultHomeHeatingSettings() {
    HomeHeatingSettings s{};
    s.heatingEnabled = true;
    s.h2Set = defaultOf(HH_KEY_H2_SET);
    s.p4OffDelayMin = defaultU32(HH_KEY_P4_OFF_DELAY);
    s.k1TravelS = defaultU32(HH_KEY_K1_TRAVEL);
    s.k1PeriodS = defaultU32(HH_KEY_K1_PERIOD);
    s.k1Deadband = defaultOf(HH_KEY_K1_DEADBAND);
    s.k1Gain = defaultOf(HH_KEY_K1_GAIN);
    s.k1MaxPulseS = defaultU32(HH_KEY_K1_MAX_PULSE);
    s.k1MinPulseS = defaultOf(HH_KEY_K1_MIN_PULSE);
    s.k1ResyncPct = defaultU32(HH_KEY_K1_RESYNC);
    s.k1SmallDiff = defaultOf(HH_KEY_K1_SMALL_DIFF);
    s.k1FailPosPct = defaultU32(HH_KEY_K1_FAIL_POS);
    s.k1FfStepPct = defaultU32(HH_KEY_K1_FF_STEP);
    s.k2Delta = defaultOf(HH_KEY_K2_DELTA);
    s.k2DeltaHyst = defaultOf(HH_KEY_K2_DELTA_HYST);
    s.k2H3Min = defaultOf(HH_KEY_K2_H3_MIN);
    s.k2H3MinHyst = defaultOf(HH_KEY_K2_H3_MIN_HYST);
    s.k2H4Max = defaultOf(HH_KEY_K2_H4_MAX);
    s.k2H4MaxHyst = defaultOf(HH_KEY_K2_H4_MAX_HYST);
    return s;
}

HomeHeatingSettings guardHomeHeatingSettings(HomeHeatingSettings s) {
    const float maxDeltaHyst = s.k2Delta - 0.5f;
    if (s.k2DeltaHyst > maxDeltaHyst) {
        s.k2DeltaHyst = maxDeltaHyst;
    }
    // ceil(k1MinPulseS) without <math.h>: truncate, then round up any fraction.
    uint32_t minPulseCeil = s.k1MinPulseS <= 0.0f ? 0u : static_cast<uint32_t>(s.k1MinPulseS);
    if (static_cast<float>(minPulseCeil) < s.k1MinPulseS) {
        ++minPulseCeil;
    }
    if (s.k1MaxPulseS < minPulseCeil) {
        s.k1MaxPulseS = minPulseCeil;
    }
    return s;
}

uint32_t k1TravelMs(const HomeHeatingSettings& s) {
    return s.k1TravelS * 1000u;
}

uint32_t k1OverdriveMs(const HomeHeatingSettings& s) {
    return static_cast<uint32_t>(static_cast<uint64_t>(k1TravelMs(s)) * s.k1ResyncPct / 100u);
}

uint32_t k1RecalMs(const HomeHeatingSettings& s) {
    return k1TravelMs(s) + k1OverdriveMs(s);
}
