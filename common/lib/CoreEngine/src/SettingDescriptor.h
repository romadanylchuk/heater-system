#pragma once
#include <stddef.h>
#include <stdint.h>

// A single settings descriptor: one row of a controller's settings table (D3/D4).
// Built only via the constexpr helper functions below, so every instance is a valid
// aggregate for its type. Numeric values are stored/compared as float everywhere
// (ints up to 2^24 are exact); Text values live in ConfigEngine's fixed string pool.
enum class SettingType : uint8_t { Int, Float, Bool, Text };

constexpr uint8_t SETTING_FLAG_NO_BACKUP = 0x01;  // excluded from backup (Wi-Fi)
constexpr uint8_t SETTING_FLAG_SECRET    = 0x02;  // value never logged/published; web masks it
constexpr uint8_t SETTING_FLAG_HA_SWITCH = 0x04;  // dashboard HA switch (stage 04)
constexpr uint8_t SETTING_FLAG_NO_HA     = 0x08;  // never exposed as an HA entity (connection/access settings)

constexpr size_t SETTING_KEY_MAX_LEN = 31;
constexpr size_t SETTING_TEXT_MAX_LEN = 64;

struct SettingDescriptor {
    const char* key;      // stable logical name (backup/web/HA), <= SETTING_KEY_MAX_LEN
    const char* nvsKey;    // explicit NVS key, <= NVS_KEY_MAX_LEN (KvStore.h)
    const char* labelEn;
    const char* labelUa;
    const char* unit;      // nullptr if none
    const char* group;     // e.g. "network", "mqtt", "access", "time", "flags"
    SettingType type;
    float minValue;        // Text: minimum length
    float maxValue;        // Text: maximum length (mirrors maxLen)
    float defaultValue;    // Text: unused (0)
    float step;            // Text: unused (0)
    const char* defaultText;  // Text only, else nullptr
    uint8_t maxLen;            // Text only (<= SETTING_TEXT_MAX_LEN), else 0
    uint8_t flags;
};

constexpr SettingDescriptor intSetting(const char* key, const char* nvsKey, const char* en, const char* ua,
        const char* unit, const char* group, int32_t min, int32_t max, int32_t def, int32_t step = 1,
        uint8_t flags = 0) {
    return SettingDescriptor{key, nvsKey, en, ua, unit, group, SettingType::Int,
        static_cast<float>(min), static_cast<float>(max), static_cast<float>(def), static_cast<float>(step),
        nullptr, 0, flags};
}

constexpr SettingDescriptor floatSetting(const char* key, const char* nvsKey, const char* en, const char* ua,
        const char* unit, const char* group, float min, float max, float def, float step = 1.0f,
        uint8_t flags = 0) {
    return SettingDescriptor{key, nvsKey, en, ua, unit, group, SettingType::Float,
        min, max, def, step, nullptr, 0, flags};
}

constexpr SettingDescriptor boolSetting(const char* key, const char* nvsKey, const char* en, const char* ua,
        const char* group, bool def, uint8_t flags = 0) {
    return SettingDescriptor{key, nvsKey, en, ua, nullptr, group, SettingType::Bool,
        0.0f, 1.0f, def ? 1.0f : 0.0f, 1.0f, nullptr, 0, flags};
}

constexpr SettingDescriptor textSetting(const char* key, const char* nvsKey, const char* en, const char* ua,
        const char* group, uint8_t minLen, uint8_t maxLen, const char* def, uint8_t flags = 0) {
    return SettingDescriptor{key, nvsKey, en, ua, nullptr, group, SettingType::Text,
        static_cast<float>(minLen), static_cast<float>(maxLen), 0.0f, 0.0f, def, maxLen, flags};
}

struct SettingsTable {
    const SettingDescriptor* items;
    size_t count;
};

template <size_t N>
constexpr SettingsTable makeTable(const SettingDescriptor (&items)[N]) {
    return {items, N};
}
