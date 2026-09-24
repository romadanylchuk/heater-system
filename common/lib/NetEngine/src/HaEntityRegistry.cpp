#include "HaEntityRegistry.h"
#include <stdio.h>
#include <string.h>
#include "NetIdentity.h"

namespace {

bool isValidHaKey(const char* key) {
    if (!key) {
        return false;
    }
    size_t len = strlen(key);
    if (len == 0 || len > HA_KEY_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = key[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Common-entity state function: Wi-Fi RSSI, unavailable while Wi-Fi is down (D9).
bool rssiState(const CommonState& state, char* out, size_t cap) {
    if (!state.network.wifiConnected) {
        return false;
    }
    snprintf(out, cap, "%d", static_cast<int>(state.network.wifiRssi));
    return true;
}

}  // namespace

const HaCustomEntity HA_COMMON_ENTITIES[] = {
    {"rssi", "Wi-Fi signal", HaComponent::Sensor, "dBm", "signal_strength", "measurement", "diagnostic", rssiState},
};
const size_t HA_COMMON_ENTITY_COUNT = sizeof(HA_COMMON_ENTITIES) / sizeof(HA_COMMON_ENTITIES[0]);

size_t haDecimalsForStep(float step) {
    if (step >= 1.0f) {
        return 0;
    }
    if (step >= 0.1f) {
        return 1;
    }
    if (step >= 0.01f) {
        return 2;
    }
    return 3;
}

bool HaEntityRegistry::insert(HaComponent comp, HaSource src, uint16_t ref, bool configCat,
    const HaCustomEntity* custom, const char* key) {
    if (_count >= HA_MAX_ENTITIES) {
        _status = HaRegistryStatus::TooManyEntities;
        _count = 0;
        return false;
    }
    for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_entities[i].key, key) == 0) {
            _status = HaRegistryStatus::DuplicateKey;
            _count = 0;
            return false;
        }
    }
    HaEntity& e = _entities[_count];
    e.component = comp;
    e.source = src;
    e.ref = ref;
    e.configCategory = configCat;
    e.custom = custom;
    strncpy(e.key, key, HA_KEY_MAX_LEN);
    e.key[HA_KEY_MAX_LEN] = '\0';
    ++_count;
    return true;
}

bool HaEntityRegistry::addCustomEntity(const HaCustomEntity* custom) {
    if (!custom || !isValidHaKey(custom->key)) {
        _status = HaRegistryStatus::BadKey;
        _count = 0;
        return false;
    }
    return insert(custom->component, HaSource::Custom, 0, false, custom, custom->key);
}

HaRegistryStatus HaEntityRegistry::build(const ConfigEngine& config, const HwProjectConfig& hw,
    const HaCustomEntity* custom, size_t customCount) {
    _count = 0;
    _status = HaRegistryStatus::Ok;
    _hw = &hw;

    // Settings, in schema order (D8).
    for (size_t i = 0; i < config.count(); ++i) {
        const SettingDescriptor* d = config.descriptor(i);
        if (!d) {
            continue;
        }
        if (d->flags & (SETTING_FLAG_SECRET | SETTING_FLAG_NO_BACKUP | SETTING_FLAG_NO_HA)) {
            continue;
        }
        HaComponent comp;
        bool configCat;
        if (d->type == SettingType::Int || d->type == SettingType::Float) {
            comp = HaComponent::Number;
            configCat = true;
        } else if (d->type == SettingType::Bool) {
            comp = HaComponent::Switch;
            configCat = (d->flags & SETTING_FLAG_HA_SWITCH) == 0;
        } else {
            continue;  // Text: never exposed
        }
        char key[HA_KEY_MAX_LEN + 1];
        if (!toSnakeKey(d->key, key, sizeof(key))) {
            _status = HaRegistryStatus::BadKey;
            _count = 0;
            return _status;
        }
        if (!insert(comp, HaSource::Setting, static_cast<uint16_t>(i), configCat, nullptr, key)) {
            return _status;
        }
    }

    // Logical sensors: temp_<snake(name)>, "<name> temperature".
    for (size_t i = 0; i < hw.sensorCount; ++i) {
        char snake[HA_KEY_MAX_LEN + 1];
        if (!toSnakeKey(hw.sensors[i].name, snake, sizeof(snake))) {
            _status = HaRegistryStatus::BadKey;
            _count = 0;
            return _status;
        }
        char key[HA_KEY_MAX_LEN + 1];
        int n = snprintf(key, sizeof(key), "temp_%s", snake);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(key)) {
            _status = HaRegistryStatus::BadKey;
            _count = 0;
            return _status;
        }
        if (!insert(HaComponent::Sensor, HaSource::LogicalSensor, static_cast<uint16_t>(i), false, nullptr, key)) {
            return _status;
        }
    }

    // Relays: relay_<snake(name)>.
    for (size_t i = 0; i < hw.relayCount; ++i) {
        char snake[HA_KEY_MAX_LEN + 1];
        if (!toSnakeKey(hw.relays[i].name, snake, sizeof(snake))) {
            _status = HaRegistryStatus::BadKey;
            _count = 0;
            return _status;
        }
        char key[HA_KEY_MAX_LEN + 1];
        int n = snprintf(key, sizeof(key), "relay_%s", snake);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(key)) {
            _status = HaRegistryStatus::BadKey;
            _count = 0;
            return _status;
        }
        if (!insert(HaComponent::BinarySensor, HaSource::Relay, static_cast<uint16_t>(i), false, nullptr, key)) {
            return _status;
        }
    }

    // HA_COMMON_ENTITIES, then project custom entities.
    for (size_t i = 0; i < HA_COMMON_ENTITY_COUNT; ++i) {
        if (!addCustomEntity(&HA_COMMON_ENTITIES[i])) {
            return _status;
        }
    }
    for (size_t i = 0; i < customCount; ++i) {
        if (!addCustomEntity(&custom[i])) {
            return _status;
        }
    }

    return _status;
}

int HaEntityRegistry::findByKey(const char* key, size_t len) const {
    if (!key) {
        return -1;
    }
    for (size_t i = 0; i < _count; ++i) {
        if (strlen(_entities[i].key) == len && strncmp(_entities[i].key, key, len) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool HaEntityRegistry::formatState(size_t i, const CommonState& s, const ConfigEngine& c, char* out,
    size_t cap) const {
    if (cap == 0) {
        return false;
    }
    if (i >= _count) {
        snprintf(out, cap, "None");
        return false;
    }
    const HaEntity& e = _entities[i];
    switch (e.source) {
        case HaSource::Setting: {
            const SettingDescriptor* d = c.descriptor(e.ref);
            if (!d) {
                snprintf(out, cap, "None");
                return false;
            }
            if (d->type == SettingType::Int) {
                snprintf(out, cap, "%d", static_cast<int>(c.getInt(e.ref)));
                return true;
            }
            if (d->type == SettingType::Float) {
                snprintf(out, cap, "%.*f", static_cast<int>(haDecimalsForStep(d->step)), c.getNumber(e.ref));
                return true;
            }
            if (d->type == SettingType::Bool) {
                snprintf(out, cap, "%s", c.getBool(e.ref) ? "ON" : "OFF");
                return true;
            }
            snprintf(out, cap, "None");
            return false;
        }
        case HaSource::LogicalSensor: {
            const LogicalSensorStatus& ls = s.sensors.sensor[e.ref];
            if (ls.state == SensorState::Ok) {
                snprintf(out, cap, "%.1f", static_cast<double>(ls.tempC));
                return true;
            }
            snprintf(out, cap, "None");
            return false;
        }
        case HaSource::Relay: {
            uint8_t channel = _hw->relays[e.ref].channel;
            snprintf(out, cap, "%s", s.relays.on[channel] ? "ON" : "OFF");
            return true;
        }
        case HaSource::Custom: {
            char buf[HA_STATE_MAX_LEN + 1];
            if (!e.custom->state(s, buf, sizeof(buf))) {
                snprintf(out, cap, "None");
                return false;
            }
            snprintf(out, cap, "%s", buf);
            return true;
        }
    }
    snprintf(out, cap, "None");
    return false;
}

const char* HaEntityRegistry::displayName(size_t i, const ConfigEngine& c, char* buf, size_t cap) const {
    if (cap == 0) {
        return buf;
    }
    if (i >= _count) {
        buf[0] = '\0';
        return buf;
    }
    const HaEntity& e = _entities[i];
    switch (e.source) {
        case HaSource::Setting: {
            const SettingDescriptor* d = c.descriptor(e.ref);
            snprintf(buf, cap, "%s", d ? d->labelEn : "");
            return buf;
        }
        case HaSource::LogicalSensor:
            snprintf(buf, cap, "%s temperature", _hw->sensors[e.ref].name);
            return buf;
        case HaSource::Relay:
            snprintf(buf, cap, "%s", _hw->relays[e.ref].name);
            return buf;
        case HaSource::Custom:
            snprintf(buf, cap, "%s", e.custom->name);
            return buf;
    }
    buf[0] = '\0';
    return buf;
}
