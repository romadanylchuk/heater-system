#pragma once
#include <stdint.h>

// Shared event catalogue: common event types/sources plus the reasons and reset
// causes used to log and classify events. Project-specific controllers extend the
// type range starting at EVENT_TYPE_PROJECT_BASE.

enum class EventType : uint16_t {
    None = 0,
    Reboot = 1,
    FactoryReset = 2,
    ConfigChanged = 3,
    ConfigClamped = 4,
    ConfigRejected = 5,
    BackupImported = 6,
    BackupRejected = 7,
    BackupNewerVersion = 8,
    ConfigMigrated = 9,
    ConfigDowngrade = 10,
    NvsError = 11,
    TimeSynced = 12,
    RtcInvalid = 13,
    RtcMissing = 14,
    TzInvalid = 15,
    WifiConnected = 20,
    WifiDisconnected = 21,
    MqttConnected = 22,
    MqttDisconnected = 23,
    RelayChanged = 30,
    AlarmRaised = 31,
    AlarmCleared = 32,
    SensorFault = 33,
    SensorRecovered = 34,
    AntiSeizeRun = 35,
    AntiFreezeRun = 36,
    RelayLockDelay = 37,
    OtaUpdate = 38,
    OtaRollback = 39,
    DiagnosticWarning = 40,
};

// Projects define their own event types with numeric values >= this base.
constexpr uint16_t EVENT_TYPE_PROJECT_BASE = 1000;

// snake_case key for known common types (for logs/web/MQTT); nullptr if the type
// is not one of the common EventType values (e.g. a project-defined type).
inline const char* eventTypeKey(uint16_t type) {
    switch (static_cast<EventType>(type)) {
        case EventType::None:                return "none";
        case EventType::Reboot:               return "reboot";
        case EventType::FactoryReset:         return "factory_reset";
        case EventType::ConfigChanged:        return "config_changed";
        case EventType::ConfigClamped:        return "config_clamped";
        case EventType::ConfigRejected:       return "config_rejected";
        case EventType::BackupImported:       return "backup_imported";
        case EventType::BackupRejected:       return "backup_rejected";
        case EventType::BackupNewerVersion:   return "backup_newer_version";
        case EventType::ConfigMigrated:       return "config_migrated";
        case EventType::ConfigDowngrade:      return "config_downgrade";
        case EventType::NvsError:             return "nvs_error";
        case EventType::TimeSynced:           return "time_synced";
        case EventType::RtcInvalid:           return "rtc_invalid";
        case EventType::RtcMissing:           return "rtc_missing";
        case EventType::TzInvalid:            return "tz_invalid";
        case EventType::WifiConnected:        return "wifi_connected";
        case EventType::WifiDisconnected:     return "wifi_disconnected";
        case EventType::MqttConnected:        return "mqtt_connected";
        case EventType::MqttDisconnected:     return "mqtt_disconnected";
        case EventType::RelayChanged:         return "relay_changed";
        case EventType::AlarmRaised:          return "alarm_raised";
        case EventType::AlarmCleared:         return "alarm_cleared";
        case EventType::SensorFault:          return "sensor_fault";
        case EventType::SensorRecovered:      return "sensor_recovered";
        case EventType::AntiSeizeRun:         return "anti_seize_run";
        case EventType::AntiFreezeRun:        return "anti_freeze_run";
        case EventType::RelayLockDelay:       return "relay_lock_delay";
        case EventType::OtaUpdate:            return "ota_update";
        case EventType::OtaRollback:          return "ota_rollback";
        case EventType::DiagnosticWarning:    return "diagnostic_warning";
        default:                              return nullptr;
    }
}

// Fixed common event sources (uint16_t).
constexpr uint16_t EVENT_SOURCE_SYSTEM = 0;
constexpr uint16_t EVENT_SOURCE_CONFIG = 1;
constexpr uint16_t EVENT_SOURCE_BACKUP = 2;
constexpr uint16_t EVENT_SOURCE_NVS = 3;
constexpr uint16_t EVENT_SOURCE_RTC = 4;
constexpr uint16_t EVENT_SOURCE_NTP = 5;
constexpr uint16_t EVENT_SOURCE_WIFI = 6;
constexpr uint16_t EVENT_SOURCE_MQTT = 7;
constexpr uint16_t EVENT_SOURCE_WATCHDOG = 8;
constexpr uint16_t EVENT_SOURCE_DI1 = 9;
constexpr uint16_t EVENT_SOURCE_WEB = 10;

// Base offsets for sources that are indexed by descriptor position (e.g.
// EVENT_SOURCE_SETTING_BASE + settingIndex).
constexpr uint16_t EVENT_SOURCE_SETTING_BASE = 0x0100;
constexpr uint16_t EVENT_SOURCE_RELAY_BASE = 0x0200;
constexpr uint16_t EVENT_SOURCE_SENSOR_BASE = 0x0300;
constexpr uint16_t EVENT_SOURCE_ALARM_BASE = 0x0400;
constexpr uint16_t EVENT_SOURCE_DIAG_BASE = 0x0500;
constexpr uint16_t EVENT_SOURCE_PROJECT_BASE = 0x1000;

enum class EventReason : uint8_t {
    None = 0,
    Boot,
    Web,
    Mqtt,
    Import,
    Logic,
    Di1,
    Ntp,
    Migration,
};

enum class ResetCause : uint8_t {
    Unknown = 0,
    PowerOn,
    External,
    Software,
    Panic,
    TaskWatchdog,
    InterruptWatchdog,
    OtherWatchdog,
    Brownout,
    DeepSleep,
};

// Sink that accepts events (rate-limited). Implemented by EventLog.
class EventSink {
public:
    virtual ~EventSink() = default;

    // Returns true if accepted (not rate-limited).
    virtual bool logEvent(uint16_t type, uint16_t source, float value, float aux, EventReason reason) = 0;
};

inline uint16_t toU16(EventType t) { return static_cast<uint16_t>(t); }
