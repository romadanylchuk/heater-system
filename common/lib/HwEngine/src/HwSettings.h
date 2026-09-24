#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonSettings.h>
#include <SettingDescriptor.h>

// Settings shared by every controller's hardware layer (D3): the relay
// min-ON/OFF lock and the anti-seize interval/time/duration. Sits as tables[1]
// of every project ConfigSchema, right after COMMON_SETTINGS_TABLE. Per-output
// anti-seize enables and sensor-address mappings are project-specific and live
// in each project's own table (tables[2]).
enum class HwSetting : uint16_t {
    RelayLock = COMMON_SETTING_COUNT,
    AsInterval,
    AsTime,
    AsDuration,
    End,
};
constexpr size_t HW_SETTING_COUNT = 4;
constexpr size_t HW_SETTINGS_END = COMMON_SETTING_COUNT + HW_SETTING_COUNT;  // first project index

inline constexpr SettingDescriptor HW_SETTINGS[] = {
    intSetting("relayLock", "relayLock", "Relay minimum ON/OFF time", "Мінімальний час увімкнення/вимкнення реле",
        "s", "relays", 0, 600, 60),
    intSetting("asInterval", "asInterval", "Anti-seize idle interval", "Інтервал простою для антизаклинювання", "d",
        "antiSeize", 1, 30, 7),
    intSetting("asTime", "asTime", "Anti-seize start time (minutes after midnight)",
        "Час запуску антизаклинювання (хв після півночі)", "min", "antiSeize", 0, 1439, 600),
    intSetting("asDuration", "asDuration", "Anti-seize run time", "Тривалість антизаклинювання", "s", "antiSeize", 5,
        300, 30),
};
inline constexpr SettingsTable HW_SETTINGS_TABLE = makeTable(HW_SETTINGS);

constexpr const char* HW_KEY_RELAY_LOCK = "relayLock";
constexpr const char* HW_KEY_AS_INTERVAL = "asInterval";
constexpr const char* HW_KEY_AS_TIME = "asTime";
constexpr const char* HW_KEY_AS_DURATION = "asDuration";

static_assert(sizeof(HW_SETTINGS) / sizeof(HW_SETTINGS[0]) == HW_SETTING_COUNT,
    "HwSetting enum out of sync with HW_SETTINGS");
