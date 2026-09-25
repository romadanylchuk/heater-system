#pragma once
#include <stddef.h>
#include <stdint.h>
#include <SettingDescriptor.h>

// OLED display settings (stage 06, D10): page rotation period and
// brightness, group "display". Appended LAST (tables[3]) to each project's
// ConfigSchema so project setting indices never move. HA-exposed like every
// non-connection setting. Plus the pure value mappings the adapter uses.
constexpr const char* DISPLAY_KEY_ROTATE_S = "dispRotateS";
constexpr const char* DISPLAY_KEY_BRIGHTNESS = "dispBright";

constexpr int32_t DISPLAY_ROTATE_S_MIN = 2, DISPLAY_ROTATE_S_MAX = 60, DISPLAY_ROTATE_S_DEFAULT = 5;
constexpr int32_t DISPLAY_BRIGHT_MIN = 1, DISPLAY_BRIGHT_MAX = 100, DISPLAY_BRIGHT_DEFAULT = 30;

inline constexpr SettingDescriptor DISPLAY_SETTINGS[] = {
    intSetting("dispRotateS", "dispRotateS", "Display page rotation period", "Період зміни сторінок дисплея",
        "s", "display", DISPLAY_ROTATE_S_MIN, DISPLAY_ROTATE_S_MAX, DISPLAY_ROTATE_S_DEFAULT),
    intSetting("dispBright", "dispBright", "Display brightness", "Яскравість дисплея",
        "%", "display", DISPLAY_BRIGHT_MIN, DISPLAY_BRIGHT_MAX, DISPLAY_BRIGHT_DEFAULT),
};
inline constexpr SettingsTable DISPLAY_SETTINGS_TABLE = makeTable(DISPLAY_SETTINGS);

constexpr size_t DISPLAY_SETTING_COUNT = 2;
static_assert(sizeof(DISPLAY_SETTINGS) / sizeof(DISPLAY_SETTINGS[0]) == DISPLAY_SETTING_COUNT,
    "DISPLAY_SETTING_COUNT out of sync with DISPLAY_SETTINGS");

// clamp pct to [1,100]; (pct * 255 + 50) / 100  -> 1%=3, 30%=77, 100%=255
constexpr uint8_t displayContrastFromPercent(int32_t pct) {
    return static_cast<uint8_t>(
        ((pct < DISPLAY_BRIGHT_MIN ? DISPLAY_BRIGHT_MIN : (pct > DISPLAY_BRIGHT_MAX ? DISPLAY_BRIGHT_MAX : pct)) * 255 +
            50) / 100);
}

// clamp s to [MIN,MAX] then * 1000
constexpr uint32_t displayRotatePeriodMs(int32_t s) {
    return static_cast<uint32_t>(
               s < DISPLAY_ROTATE_S_MIN ? DISPLAY_ROTATE_S_MIN : (s > DISPLAY_ROTATE_S_MAX ? DISPLAY_ROTATE_S_MAX : s)) *
        1000u;
}
