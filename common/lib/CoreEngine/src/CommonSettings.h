#pragma once
#include <stddef.h>
#include <stdint.h>
#include "SettingDescriptor.h"

// The settings every controller shares: Wi-Fi, MQTT, web login, time zone and NTP
// server. Always tables[0] of a ConfigSchema (D4). Header-only inline constexpr, so
// COMMON_SETTINGS/COMMON_SETTINGS_TABLE have a single address program-wide (D2).
enum class CommonSetting : uint16_t {
    WifiSsid = 0,
    WifiPass,
    MqttHost,
    MqttPort,
    MqttUser,
    MqttPass,
    WebUser,
    WebPass,
    Tz,
    NtpServer,
    Count,
};
constexpr size_t COMMON_SETTING_COUNT = static_cast<size_t>(CommonSetting::Count);

constexpr const char* DEFAULT_TZ = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // Europe/Kyiv, automatic DST

inline constexpr SettingDescriptor COMMON_SETTINGS[] = {
    textSetting("wifiSsid", "wifiSsid", "Wi-Fi network", "Мережа Wi-Fi", "network", 0, 32, "",
        SETTING_FLAG_NO_BACKUP),
    textSetting("wifiPass", "wifiPass", "Wi-Fi password", "Пароль Wi-Fi", "network", 0, 64, "",
        SETTING_FLAG_NO_BACKUP | SETTING_FLAG_SECRET),
    textSetting("mqttHost", "mqttHost", "MQTT broker host", "Хост MQTT-брокера", "mqtt", 0, 64, "",
        SETTING_FLAG_NO_HA),
    intSetting("mqttPort", "mqttPort", "MQTT broker port", "Порт MQTT-брокера", nullptr, "mqtt", 1, 65535, 1883, 1,
        SETTING_FLAG_NO_HA),
    textSetting("mqttUser", "mqttUser", "MQTT username", "Користувач MQTT", "mqtt", 0, 32, "",
        SETTING_FLAG_NO_HA),
    textSetting("mqttPass", "mqttPass", "MQTT password", "Пароль MQTT", "mqtt", 0, 64, "", SETTING_FLAG_SECRET),
    textSetting("webUser", "webUser", "Web login", "Логін веб-інтерфейсу", "access", 1, 32, "admin",
        SETTING_FLAG_NO_HA),
    textSetting("webPass", "webPass", "Web password", "Пароль веб-інтерфейсу", "access", 1, 64, "admin",
        SETTING_FLAG_SECRET),
    textSetting("tz", "tz", "Time zone (POSIX)", "Часовий пояс (POSIX)", "time", 1, 48, DEFAULT_TZ,
        SETTING_FLAG_NO_HA),
    textSetting("ntpServer", "ntpServer", "NTP server", "Сервер NTP", "time", 1, 64, "pool.ntp.org",
        SETTING_FLAG_NO_HA),
};
inline constexpr SettingsTable COMMON_SETTINGS_TABLE = makeTable(COMMON_SETTINGS);
static_assert(sizeof(COMMON_SETTINGS) / sizeof(COMMON_SETTINGS[0]) == COMMON_SETTING_COUNT,
    "CommonSetting enum out of sync with COMMON_SETTINGS");

constexpr size_t commonIndex(CommonSetting s) { return static_cast<size_t>(s); }
