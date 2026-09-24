#include "BackupCodec.h"

#include <ArduinoJson.h>
#include <string.h>
#include "SettingClamp.h"

namespace BackupCodec {

namespace {

BackupImportResult reject(EventSink& events, BackupStatus status) {
    events.logEvent(toU16(EventType::BackupRejected), EVENT_SOURCE_BACKUP,
        static_cast<float>(static_cast<uint8_t>(status)), 0, EventReason::Import);
    return BackupImportResult{status, 0, 0, 0, 0, false};
}

// Maps a backup-file logical key through every rename whose toVersion is above
// backupVersion (D7/D18), chained across versions, so an older backup's keys land
// on the current schema. A newer-or-equal backup has no migration with
// toVersion > backupVersion (migrations are bounded by configVersion), so this is
// a no-op in that case.
const char* renameKey(const ConfigSchema& schema, const char* key, uint16_t backupVersion) {
    for (size_t m = 0; m < schema.migrationCount; ++m) {
        const ConfigMigration& mig = schema.migrations[m];
        if (mig.toVersion <= backupVersion) {
            continue;
        }
        for (size_t r = 0; r < mig.renameCount; ++r) {
            if (strcmp(key, mig.renames[r].oldKey) == 0) {
                key = mig.renames[r].newKey;
                break;
            }
        }
    }
    return key;
}

}  // namespace

size_t exportJson(const ConfigEngine& config, const char* fwVersion, char* out, size_t cap) {
    if (!config.isReady()) {
        return 0;
    }

    JsonDocument doc;
    doc["type"] = config.schema().controllerType;
    doc["format"] = BACKUP_FORMAT;
    doc["configVersion"] = config.schema().configVersion;
    doc["fwVersion"] = fwVersion != nullptr ? fwVersion : "";

    JsonObject settings = doc["settings"].to<JsonObject>();
    for (size_t i = 0; i < config.count(); ++i) {
        const SettingDescriptor* d = config.descriptor(i);
        if (d == nullptr || (d->flags & SETTING_FLAG_NO_BACKUP)) {
            continue;
        }
        switch (d->type) {
            case SettingType::Int:
                settings[d->key] = config.getInt(i);
                break;
            case SettingType::Float:
                settings[d->key] = config.getNumber(i);
                break;
            case SettingType::Bool:
                settings[d->key] = config.getBool(i);
                break;
            case SettingType::Text:
                settings[d->key] = config.getText(i);
                break;
        }
    }

    if (measureJson(doc) + 1 > cap) {
        return 0;
    }
    return serializeJson(doc, out, cap);
}

BackupImportResult importJson(ConfigEngine& config, EventSink& events, const char* json, size_t len,
        uint64_t monoMs) {
    if (!config.isReady()) {
        return BackupImportResult{BackupStatus::NotReady, 0, 0, 0, 0, false};
    }
    if (json == nullptr || len > BACKUP_MAX_BYTES) {
        return reject(events, BackupStatus::Corrupt);
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len, DeserializationOption::NestingLimit(4));
    if (err) {
        return reject(events, BackupStatus::Corrupt);
    }
    if (!doc.is<JsonObject>()) {
        return reject(events, BackupStatus::Corrupt);
    }
    JsonObject root = doc.as<JsonObject>();

    JsonVariant settingsVar = root["settings"];
    if (!settingsVar.is<JsonObject>()) {
        return reject(events, BackupStatus::Corrupt);
    }
    JsonObject settingsObj = settingsVar.as<JsonObject>();

    JsonVariant typeVar = root["type"];
    if (!typeVar.is<const char*>()) {
        return reject(events, BackupStatus::MissingType);
    }
    if (strcmp(typeVar.as<const char*>(), config.schema().controllerType) != 0) {
        return reject(events, BackupStatus::WrongType);
    }

    JsonVariant formatVar = root["format"];
    if (!formatVar.is<int>() || formatVar.as<int>() != static_cast<int>(BACKUP_FORMAT)) {
        return reject(events, BackupStatus::UnsupportedFormat);
    }

    JsonVariant versionVar = root["configVersion"];
    if (!versionVar.is<int>() || versionVar.as<int>() < 1) {
        return reject(events, BackupStatus::Corrupt);
    }
    uint16_t backupVersion = static_cast<uint16_t>(versionVar.as<int>());

    // Pass 1: validate every known, non-NO_BACKUP key's JSON type/length. Any
    // failure refuses the whole file with zero changes (no partial apply).
    uint16_t ignoredUnknown = 0;
    for (JsonPair kv : settingsObj) {
        const char* mappedKey = renameKey(config.schema(), kv.key().c_str(), backupVersion);
        int idx = config.indexOf(mappedKey);
        if (idx < 0) {
            ++ignoredUnknown;
            continue;
        }
        const SettingDescriptor* d = config.descriptor(static_cast<size_t>(idx));
        if (d->flags & SETTING_FLAG_NO_BACKUP) {
            ++ignoredUnknown;
            continue;
        }
        JsonVariant v = kv.value();
        switch (d->type) {
            case SettingType::Int:
            case SettingType::Float:
                if (!v.is<int>() && !v.is<float>()) {
                    return reject(events, BackupStatus::InvalidValue);
                }
                break;
            case SettingType::Bool:
                if (!v.is<bool>() && !v.is<int>() && !v.is<float>()) {
                    return reject(events, BackupStatus::InvalidValue);
                }
                break;
            case SettingType::Text:
                if (!v.is<const char*>() || !isTextLengthValid(*d, v.as<const char*>())) {
                    return reject(events, BackupStatus::InvalidValue);
                }
                break;
        }
    }

    // Pass 2: apply. Missing keys keep their current value (D18); unknown/
    // NO_BACKUP keys were already counted above and are skipped again here.
    uint16_t applied = 0;
    uint16_t clamped = 0;
    for (JsonPair kv : settingsObj) {
        const char* mappedKey = renameKey(config.schema(), kv.key().c_str(), backupVersion);
        int idx = config.indexOf(mappedKey);
        if (idx < 0) {
            continue;
        }
        const SettingDescriptor* d = config.descriptor(static_cast<size_t>(idx));
        if (d->flags & SETTING_FLAG_NO_BACKUP) {
            continue;
        }
        JsonVariant v = kv.value();
        ConfigStatus st;
        if (d->type == SettingType::Text) {
            st = config.setText(static_cast<size_t>(idx), v.as<const char*>(), EventReason::Import, monoMs);
        } else if (d->type == SettingType::Bool) {
            bool bv = v.is<bool>() ? v.as<bool>() : (v.as<float>() != 0.0f);
            st = config.setNumber(static_cast<size_t>(idx), bv ? 1.0f : 0.0f, EventReason::Import, monoMs);
        } else {
            st = config.setNumber(static_cast<size_t>(idx), v.as<float>(), EventReason::Import, monoMs);
        }
        if (st == ConfigStatus::Ok || st == ConfigStatus::Clamped) {
            ++applied;
            if (st == ConfigStatus::Clamped) {
                ++clamped;
            }
        }
    }

    config.flushNow();

    BackupImportResult result{BackupStatus::Ok, applied, clamped, ignoredUnknown, backupVersion,
        backupVersion > config.schema().configVersion};

    if (result.newerVersion) {
        events.logEvent(toU16(EventType::BackupNewerVersion), EVENT_SOURCE_BACKUP,
            static_cast<float>(backupVersion), static_cast<float>(config.schema().configVersion),
            EventReason::Import);
    }
    events.logEvent(toU16(EventType::BackupImported), EVENT_SOURCE_BACKUP, static_cast<float>(applied),
        static_cast<float>(ignoredUnknown), EventReason::Import);

    return result;
}

}  // namespace BackupCodec
