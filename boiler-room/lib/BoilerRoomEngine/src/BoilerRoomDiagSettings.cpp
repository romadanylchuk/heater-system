#include "BoilerRoomDiagSettings.h"
#include <string.h>

namespace {

// Default of the row with the given key; the key set is fixed at compile time
// and covered by tests, so a miss (0) can only be a programming error.
float defaultOf(const char* key) {
    for (size_t i = 0; i < BOILER_ROOM_DIAG_SETTING_COUNT; ++i) {
        if (strcmp(BOILER_ROOM_DIAG_SETTINGS[i].key, key) == 0) {
            return BOILER_ROOM_DIAG_SETTINGS[i].defaultValue;
        }
    }
    return 0.0f;
}

uint32_t defaultU32(const char* key) {
    float v = defaultOf(key);
    return v <= 0.0f ? 0u : static_cast<uint32_t>(v);
}

RiseCheckParams defaultParams(const char* en, const char* minOn, const char* delta, const char* minRise) {
    RiseCheckParams p{};
    p.enabled = defaultOf(en) != 0.0f;
    p.minOnMin = defaultU32(minOn);
    p.deltaC = defaultOf(delta);
    p.minRiseC = defaultOf(minRise);
    return p;
}

}  // namespace

BoilerRoomDiagSettings defaultBoilerRoomDiagSettings() {
    BoilerRoomDiagSettings s{};
    s.b1 = defaultParams(BR_KEY_B1_EN, BR_KEY_B1_MIN_ON, BR_KEY_B1_DELTA, BR_KEY_B1_MIN_RISE);
    s.b3 = defaultParams(BR_KEY_B3_EN, BR_KEY_B3_MIN_ON, BR_KEY_B3_DELTA, BR_KEY_B3_MIN_RISE);
    s.b6 = defaultParams(BR_KEY_B6_EN, BR_KEY_B6_MIN_ON, BR_KEY_B6_DELTA, BR_KEY_B6_MIN_RISE);
    return s;
}
