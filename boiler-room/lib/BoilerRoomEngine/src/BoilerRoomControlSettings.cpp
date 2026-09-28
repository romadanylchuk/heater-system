#include "BoilerRoomControlSettings.h"
#include <string.h>

namespace {

// Default of the row with the given key; the key set is fixed at compile time
// and covered by tests, so a miss (0) can only be a programming error.
float defaultOf(const char* key) {
    for (size_t i = 0; i < BOILER_ROOM_CONTROL_SETTING_COUNT; ++i) {
        if (strcmp(BOILER_ROOM_CONTROL_SETTINGS[i].key, key) == 0) {
            return BOILER_ROOM_CONTROL_SETTINGS[i].defaultValue;
        }
    }
    return 0.0f;
}

uint32_t defaultU32(const char* key) {
    float v = defaultOf(key);
    return v <= 0.0f ? 0u : static_cast<uint32_t>(v);
}

}  // namespace

BoilerRoomSettings defaultBoilerRoomSettings() {
    BoilerRoomSettings s{};
    s.p1DeltaOn = defaultOf(BR_KEY_P1_DELTA_ON);
    s.p1DeltaOff = defaultOf(BR_KEY_P1_DELTA_OFF);
    s.p1T1Min = defaultOf(BR_KEY_P1_T1_MIN);
    s.p1Hyst = defaultOf(BR_KEY_P1_HYST);
    s.ohOn = defaultOf(BR_KEY_OH_ON);
    s.ohClear = defaultOf(BR_KEY_OH_CLEAR);
    s.p2T2Off = defaultOf(BR_KEY_P2_T2_OFF);
    s.p2Hyst = defaultOf(BR_KEY_P2_HYST);
    s.p2T1Burn = defaultOf(BR_KEY_P2_T1_BURN);
    s.p3T3Offer = defaultOf(BR_KEY_P3_T3_OFFER);
    s.p3OfferWindowMin = defaultU32(BR_KEY_P3_OFFER_WIN);
    s.p3OfferWaitMin = defaultU32(BR_KEY_P3_OFFER_WAIT);
    s.afEnable = defaultOf(BR_KEY_AF_ENABLE) != 0.0f;
    s.afIntervalMin = defaultU32(BR_KEY_AF_INTERVAL);
    s.afDurationS = defaultU32(BR_KEY_AF_DURATION);
    s.accVolumeL = defaultOf(BR_KEY_ACC_VOLUME);
    s.accTBase = defaultOf(BR_KEY_ACC_T_BASE);
    return s;
}

BoilerRoomSettings guardBoilerRoomSettings(BoilerRoomSettings s) {
    const float maxDeltaOff = s.p1DeltaOn - 0.5f;
    if (s.p1DeltaOff > maxDeltaOff) {
        s.p1DeltaOff = maxDeltaOff;
    }
    const float maxOhClear = s.ohOn - 1.0f;
    if (s.ohClear > maxOhClear) {
        s.ohClear = maxOhClear;
    }
    return s;
}
