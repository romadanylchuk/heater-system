#include "ConfigEngine.h"

#include <math.h>
#include <string.h>
#include "CommonSettings.h"
#include "SettingClamp.h"

namespace {

bool tableMatchesCommon(const SettingsTable& t0) {
    if (t0.count != COMMON_SETTING_COUNT || t0.items == nullptr) {
        return false;
    }
    for (size_t i = 0; i < COMMON_SETTING_COUNT; ++i) {
        if (t0.items[i].key == nullptr || strcmp(t0.items[i].key, COMMON_SETTINGS[i].key) != 0) {
            return false;
        }
    }
    return true;
}

float maskIfSecret(uint8_t flags, float value) {
    return (flags & SETTING_FLAG_SECRET) ? 0.0f : value;
}

}  // namespace

ConfigEngine::ConfigEngine(KvStore& store, EventSink& events) : _store(store), _events(events) {}

bool ConfigEngine::validateSchema(const ConfigSchema& schema) {
    if (schema.controllerType == nullptr || schema.controllerType[0] == '\0') {
        return false;
    }
    if (schema.configVersion < 1) {
        return false;
    }
    if (schema.tableCount < 1 || schema.tables == nullptr) {
        return false;
    }
    if (!tableMatchesCommon(schema.tables[0])) {
        return false;
    }

    size_t total = 0;
    for (size_t ti = 0; ti < schema.tableCount; ++ti) {
        total += schema.tables[ti].count;
    }
    if (total < 1 || total > CONFIG_MAX_SETTINGS) {
        return false;
    }

    const SettingDescriptor* flat[CONFIG_MAX_SETTINGS];
    size_t idx = 0;
    for (size_t ti = 0; ti < schema.tableCount; ++ti) {
        const SettingsTable& t = schema.tables[ti];
        for (size_t i = 0; i < t.count; ++i) {
            flat[idx++] = &t.items[i];
        }
    }

    size_t stringPoolNeeded = 0;
    for (size_t i = 0; i < idx; ++i) {
        const SettingDescriptor& d = *flat[i];
        if (d.key == nullptr) {
            return false;
        }
        size_t keyLen = strlen(d.key);
        if (keyLen < 1 || keyLen > SETTING_KEY_MAX_LEN) {
            return false;
        }
        if (d.nvsKey == nullptr) {
            return false;
        }
        size_t nvsLen = strlen(d.nvsKey);
        if (nvsLen < 1 || nvsLen > NVS_KEY_MAX_LEN) {
            return false;
        }
        if (strcmp(d.nvsKey, CONFIG_VERSION_KEY) == 0) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(flat[j]->key, d.key) == 0) {
                return false;
            }
            if (strcmp(flat[j]->nvsKey, d.nvsKey) == 0) {
                return false;
            }
        }

        if (d.type == SettingType::Text) {
            if (d.maxLen < 1 || d.maxLen > SETTING_TEXT_MAX_LEN) {
                return false;
            }
            if (!isTextLengthValid(d, d.defaultText)) {
                return false;
            }
            stringPoolNeeded += static_cast<size_t>(d.maxLen) + 1;
        } else {
            if (!(d.minValue <= d.defaultValue && d.defaultValue <= d.maxValue)) {
                return false;
            }
        }
    }
    if (stringPoolNeeded > CONFIG_STRING_POOL_BYTES) {
        return false;
    }

    uint16_t prevVersion = 0;
    for (size_t m = 0; m < schema.migrationCount; ++m) {
        const ConfigMigration& mig = schema.migrations[m];
        if (mig.toVersion <= prevVersion || mig.toVersion > schema.configVersion) {
            return false;
        }
        prevVersion = mig.toVersion;

        for (size_t r = 0; r < mig.renameCount; ++r) {
            const KeyRename& rn = mig.renames[r];
            bool foundKey = false;
            bool foundNvs = false;
            for (size_t i = 0; i < idx; ++i) {
                if (strcmp(flat[i]->key, rn.newKey) == 0) {
                    foundKey = true;
                }
                if (strcmp(flat[i]->nvsKey, rn.newNvsKey) == 0) {
                    foundNvs = true;
                }
            }
            if (!foundKey || !foundNvs) {
                return false;
            }
        }
    }

    return true;
}

ConfigStatus ConfigEngine::begin(const ConfigSchema& schema, uint64_t monoMs) {
    _ready = false;
    _schema = &schema;

    if (!validateSchema(schema)) {
        return ConfigStatus::SchemaInvalid;
    }

    _count = 0;
    size_t poolCursor = 0;
    for (size_t ti = 0; ti < schema.tableCount; ++ti) {
        const SettingsTable& t = schema.tables[ti];
        for (size_t i = 0; i < t.count; ++i) {
            const SettingDescriptor& d = t.items[i];
            size_t index = _count++;
            _descriptors[index] = &d;
            _dirty[index] = false;
            if (d.type == SettingType::Text) {
                _stringOffset[index] = poolCursor;
                size_t slice = static_cast<size_t>(d.maxLen) + 1;
                memset(&_stringPool[poolCursor], 0, slice);
                strncpy(&_stringPool[poolCursor], d.defaultText, d.maxLen);
                poolCursor += slice;
            } else {
                _numbers[index] = d.defaultValue;
            }
        }
    }

    _anyDirty = false;
    _lastChangeMs = monoMs;
    _lastStoreOk = true;
    _storedVersionAtBoot = 0;
    _ready = true;

    loadFromStore(monoMs);
    return _lastStoreOk ? ConfigStatus::Ok : ConfigStatus::StoreError;
}

void ConfigEngine::loadFromStore(uint64_t /*monoMs*/) {
    int32_t storedVerRaw = 0;
    StoreStatus verStatus = _store.getI32(CONFIG_VERSION_KEY, storedVerRaw);

    if (verStatus == StoreStatus::NotFound && !_store.hasAnyKey()) {
        // Fresh device: nothing stored, everything already at defaults.
        StoreStatus writeSt = _store.setI32(CONFIG_VERSION_KEY, static_cast<int32_t>(_schema->configVersion));
        StoreStatus commitSt = _store.commit();
        if (writeSt != StoreStatus::Ok || commitSt != StoreStatus::Ok) {
            _lastStoreOk = false;
            logConfigEvent(EventType::NvsError, EVENT_SOURCE_NVS, 0, 0, EventReason::Boot);
        }
        return;
    }

    bool storeErrorAtVersionRead = false;
    if (verStatus == StoreStatus::NotFound) {
        _storedVersionAtBoot = 0;  // keys present, cfgVer missing: treat as version 0
    } else if (verStatus == StoreStatus::Ok) {
        _storedVersionAtBoot = static_cast<uint16_t>(storedVerRaw);
    } else {
        storeErrorAtVersionRead = true;
    }

    if (!storeErrorAtVersionRead) {
        if (_storedVersionAtBoot < _schema->configVersion) {
            uint16_t before = _storedVersionAtBoot;
            runMigrations(*_schema, before);
            _store.setI32(CONFIG_VERSION_KEY, static_cast<int32_t>(_schema->configVersion));
            _store.commit();
            logConfigEvent(EventType::ConfigMigrated, EVENT_SOURCE_CONFIG, static_cast<float>(before),
                static_cast<float>(_schema->configVersion), EventReason::Migration);
        } else if (_storedVersionAtBoot > _schema->configVersion) {
            logConfigEvent(EventType::ConfigDowngrade, EVENT_SOURCE_CONFIG,
                static_cast<float>(_storedVersionAtBoot), static_cast<float>(_schema->configVersion),
                EventReason::Boot);
        }
    }

    bool anyDirtyAfterLoad = false;
    for (size_t index = 0; index < _count; ++index) {
        const SettingDescriptor& d = *_descriptors[index];
        if (d.type == SettingType::Text) {
            char buf[SETTING_TEXT_MAX_LEN + 1];
            StoreStatus st = _store.getStr(d.nvsKey, buf, sizeof(buf));
            if (st == StoreStatus::NotFound) {
                continue;
            }
            if (st == StoreStatus::Ok) {
                if (isTextLengthValid(d, buf)) {
                    setStringValue(index, buf);
                } else {
                    logSettingEvent(EventType::ConfigRejected, index, 0, 0, EventReason::Boot);
                }
            } else if (st == StoreStatus::TooLarge || st == StoreStatus::TypeMismatch) {
                logSettingEvent(EventType::ConfigRejected, index, 0, 0, EventReason::Boot);
            }
            // else (OpenFailed/ReadFailed): leave the default silently; the single
            // NvsError event logged at the end of loadFromStore() already covers this.
        } else {
            float raw = 0.0f;
            StoreStatus st;
            if (d.type == SettingType::Float) {
                st = _store.getFloat(d.nvsKey, raw);
            } else {
                int32_t i32 = 0;
                st = _store.getI32(d.nvsKey, i32);
                raw = static_cast<float>(i32);
            }
            if (st == StoreStatus::NotFound) {
                continue;
            }
            if (st == StoreStatus::Ok) {
                ClampResult cr = clampNumber(d, raw);
                if (cr.outcome == ClampOutcome::Rejected) {
                    logSettingEvent(EventType::ConfigRejected, index, 0, 0, EventReason::Boot);
                } else {
                    _numbers[index] = cr.value;
                    if (cr.outcome == ClampOutcome::Clamped) {
                        _dirty[index] = true;
                        anyDirtyAfterLoad = true;
                        logSettingEvent(EventType::ConfigClamped, index, maskIfSecret(d.flags, cr.value),
                            maskIfSecret(d.flags, raw), EventReason::Boot);
                    }
                }
            } else if (st == StoreStatus::TooLarge || st == StoreStatus::TypeMismatch) {
                logSettingEvent(EventType::ConfigRejected, index, 0, 0, EventReason::Boot);
            }
            // else (OpenFailed/ReadFailed): leave the default silently; the single
            // NvsError event logged at the end of loadFromStore() already covers this.
        }
    }

    if (anyDirtyAfterLoad) {
        flushNow();
    }

    if (storeErrorAtVersionRead) {
        _lastStoreOk = false;
        logConfigEvent(EventType::NvsError, EVENT_SOURCE_NVS, 0, 0, EventReason::Boot);
    }
}

const SettingDescriptor* ConfigEngine::descriptor(size_t index) const {
    if (index >= _count) {
        return nullptr;
    }
    return _descriptors[index];
}

int ConfigEngine::indexOf(const char* key) const {
    if (key == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_descriptors[i]->key, key) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float ConfigEngine::getNumber(size_t index) const {
    if (index >= _count || _descriptors[index]->type == SettingType::Text) {
        return 0.0f;
    }
    return _numbers[index];
}

int32_t ConfigEngine::getInt(size_t index) const {
    return static_cast<int32_t>(lroundf(getNumber(index)));
}

bool ConfigEngine::getBool(size_t index) const {
    return getNumber(index) != 0.0f;
}

const char* ConfigEngine::getText(size_t index) const {
    if (index >= _count || _descriptors[index]->type != SettingType::Text) {
        return "";
    }
    return &_stringPool[_stringOffset[index]];
}

ConfigStatus ConfigEngine::setNumber(size_t index, float value, EventReason origin, uint64_t monoMs) {
    if (index >= _count) {
        return ConfigStatus::InvalidIndex;
    }
    const SettingDescriptor& d = *_descriptors[index];
    if (d.type == SettingType::Text) {
        return ConfigStatus::TypeMismatch;
    }

    ClampResult cr = clampNumber(d, value);
    if (cr.outcome == ClampOutcome::Rejected) {
        float logValue = isfinite(value) ? value : 0.0f;
        logSettingEvent(EventType::ConfigRejected, index, maskIfSecret(d.flags, logValue), 0, origin);
        return ConfigStatus::Rejected;
    }

    float oldValue = _numbers[index];
    if (cr.value == oldValue) {
        return ConfigStatus::Unchanged;
    }

    _numbers[index] = cr.value;
    _dirty[index] = true;
    _anyDirty = true;
    _lastChangeMs = monoMs;

    if (cr.outcome == ClampOutcome::Clamped) {
        logSettingEvent(EventType::ConfigClamped, index, maskIfSecret(d.flags, cr.value),
            maskIfSecret(d.flags, value), origin);
    } else {
        logSettingEvent(EventType::ConfigChanged, index, maskIfSecret(d.flags, cr.value),
            maskIfSecret(d.flags, oldValue), origin);
    }

    if (_hook) {
        _hook(index, _hookCtx);
    }
    return cr.outcome == ClampOutcome::Clamped ? ConfigStatus::Clamped : ConfigStatus::Ok;
}

ConfigStatus ConfigEngine::setText(size_t index, const char* value, EventReason origin, uint64_t monoMs) {
    if (index >= _count) {
        return ConfigStatus::InvalidIndex;
    }
    const SettingDescriptor& d = *_descriptors[index];
    if (d.type != SettingType::Text) {
        return ConfigStatus::TypeMismatch;
    }

    if (!isTextLengthValid(d, value)) {
        logSettingEvent(EventType::ConfigRejected, index, 0, 0, origin);
        return ConfigStatus::Rejected;
    }

    if (strcmp(getText(index), value) == 0) {
        return ConfigStatus::Unchanged;
    }

    setStringValue(index, value);
    _dirty[index] = true;
    _anyDirty = true;
    _lastChangeMs = monoMs;

    // Text always logs value = aux = 0 (D9): the value itself is never put in the log.
    logSettingEvent(EventType::ConfigChanged, index, 0, 0, origin);

    if (_hook) {
        _hook(index, _hookCtx);
    }
    return ConfigStatus::Ok;
}

ConfigStatus ConfigEngine::tick(uint64_t monoMs) {
    if (!_anyDirty) {
        return ConfigStatus::Ok;
    }
    if (monoMs - _lastChangeMs >= CONFIG_SAVE_DEBOUNCE_MS) {
        return flushNow();
    }
    return ConfigStatus::Ok;
}

ConfigStatus ConfigEngine::flushNow() {
    bool allOk = true;
    for (size_t index = 0; index < _count; ++index) {
        if (!_dirty[index]) {
            continue;
        }
        const SettingDescriptor& d = *_descriptors[index];
        StoreStatus st;
        if (d.type == SettingType::Text) {
            st = _store.setStr(d.nvsKey, getText(index));
        } else if (d.type == SettingType::Float) {
            st = _store.setFloat(d.nvsKey, _numbers[index]);
        } else {
            st = _store.setI32(d.nvsKey, static_cast<int32_t>(_numbers[index]));
        }
        if (st == StoreStatus::Ok) {
            _dirty[index] = false;
        } else {
            allOk = false;
        }
    }

    StoreStatus commitSt = _store.commit();
    if (commitSt != StoreStatus::Ok) {
        allOk = false;
    }

    _anyDirty = false;
    for (size_t i = 0; i < _count; ++i) {
        if (_dirty[i]) {
            _anyDirty = true;
            break;
        }
    }

    if (!allOk) {
        _lastStoreOk = false;
        logConfigEvent(EventType::NvsError, EVENT_SOURCE_NVS, 0, 0, EventReason::None);
        return ConfigStatus::StoreError;
    }
    _lastStoreOk = true;
    return ConfigStatus::Ok;
}

ConfigStatus ConfigEngine::factoryReset() {
    StoreStatus eraseSt = _store.eraseAll();

    for (size_t index = 0; index < _count; ++index) {
        const SettingDescriptor& d = *_descriptors[index];
        if (d.type == SettingType::Text) {
            setStringValue(index, d.defaultText);
        } else {
            _numbers[index] = d.defaultValue;
        }
        _dirty[index] = false;
    }
    _anyDirty = false;

    StoreStatus verSt = _store.setI32(CONFIG_VERSION_KEY, static_cast<int32_t>(_schema->configVersion));
    StoreStatus commitSt = _store.commit();

    bool ok = eraseSt == StoreStatus::Ok && verSt == StoreStatus::Ok && commitSt == StoreStatus::Ok;
    _lastStoreOk = ok;
    return ok ? ConfigStatus::Ok : ConfigStatus::StoreError;
}

void ConfigEngine::setStringValue(size_t index, const char* value) {
    const SettingDescriptor& d = *_descriptors[index];
    char* dst = &_stringPool[_stringOffset[index]];
    size_t len = strlen(value);
    if (len > d.maxLen) {
        len = d.maxLen;
    }
    memcpy(dst, value, len);
    dst[len] = '\0';
}

void ConfigEngine::logConfigEvent(EventType type, uint16_t source, float value, float aux, EventReason reason) {
    _events.logEvent(toU16(type), source, value, aux, reason);
}

void ConfigEngine::logSettingEvent(EventType type, size_t index, float value, float aux, EventReason reason) {
    logConfigEvent(type, static_cast<uint16_t>(EVENT_SOURCE_SETTING_BASE + index), value, aux, reason);
}

void ConfigEngine::runMigrations(const ConfigSchema& schema, uint16_t storedVersion) {
    for (size_t m = 0; m < schema.migrationCount; ++m) {
        const ConfigMigration& mig = schema.migrations[m];
        if (mig.toVersion <= storedVersion) {
            continue;
        }
        for (size_t r = 0; r < mig.renameCount; ++r) {
            applyRename(mig.renames[r]);
        }
    }
}

void ConfigEngine::applyRename(const KeyRename& rename) {
    const SettingDescriptor* target = nullptr;
    for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_descriptors[i]->key, rename.newKey) == 0) {
            target = _descriptors[i];
            break;
        }
    }
    if (target == nullptr) {
        return;  // schema validation guarantees this exists; defensive no-op otherwise
    }

    if (target->type == SettingType::Text) {
        char buf[SETTING_TEXT_MAX_LEN + 1];
        if (_store.getStr(rename.oldNvsKey, buf, sizeof(buf)) == StoreStatus::Ok) {
            char existing[SETTING_TEXT_MAX_LEN + 1];
            if (_store.getStr(rename.newNvsKey, existing, sizeof(existing)) == StoreStatus::NotFound) {
                _store.setStr(rename.newNvsKey, buf);
            }
        }
    } else if (target->type == SettingType::Float) {
        float v = 0.0f;
        if (_store.getFloat(rename.oldNvsKey, v) == StoreStatus::Ok) {
            float existing = 0.0f;
            if (_store.getFloat(rename.newNvsKey, existing) == StoreStatus::NotFound) {
                _store.setFloat(rename.newNvsKey, v);
            }
        }
    } else {
        int32_t v = 0;
        if (_store.getI32(rename.oldNvsKey, v) == StoreStatus::Ok) {
            int32_t existing = 0;
            if (_store.getI32(rename.newNvsKey, existing) == StoreStatus::NotFound) {
                _store.setI32(rename.newNvsKey, v);
            }
        }
    }
    _store.remove(rename.oldNvsKey);
}
