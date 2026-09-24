#include "HaDiscovery.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <EventTypes.h>
#include <HwConfig.h>

const char* haComponentName(HaComponent c) {
    switch (c) {
        case HaComponent::Sensor:
            return "sensor";
        case HaComponent::BinarySensor:
            return "binary_sensor";
        case HaComponent::Switch:
            return "switch";
        case HaComponent::Number:
            return "number";
    }
    return "sensor";
}

namespace {

bool fits(int n, size_t cap) { return n > 0 && static_cast<size_t>(n) < cap; }

}  // namespace

bool buildStateTopic(const NetIdentity& id, const HaEntity& e, char* out, size_t cap) {
    return fits(snprintf(out, cap, "%s/%s/state", id.prefix, e.key), cap);
}

bool buildCommandTopic(const NetIdentity& id, const HaEntity& e, char* out, size_t cap) {
    return fits(snprintf(out, cap, "%s/%s/set", id.prefix, e.key), cap);
}

bool buildDiscoveryTopic(const NetIdentity& id, const HaEntity& e, char* out, size_t cap) {
    return fits(snprintf(out, cap, "%s/%s/%s/%s/config", HA_DISCOVERY_PREFIX, haComponentName(e.component),
                     id.prefix, e.key),
        cap);
}

bool buildAvailabilityTopic(const NetIdentity& id, char* out, size_t cap) {
    return fits(snprintf(out, cap, "%s/status", id.prefix), cap);
}

bool buildEventTopic(const NetIdentity& id, char* out, size_t cap) {
    return fits(snprintf(out, cap, "%s/event", id.prefix), cap);
}

bool buildCommandSubscription(const NetIdentity& id, char* out, size_t cap) {
    return fits(snprintf(out, cap, "%s/+/set", id.prefix), cap);
}

size_t buildDiscoveryPayload(const HaEntityRegistry& reg, size_t i, const ConfigEngine& config,
    const NetIdentity& id, const char* fwVersion, char* out, size_t cap) {
    if (i >= reg.count()) {
        return 0;
    }
    const HaEntity& e = reg.entity(i);

    char nameBuf[64];
    reg.displayName(i, config, nameBuf, sizeof(nameBuf));

    char uniqueId[HA_KEY_MAX_LEN + NET_PREFIX_MAX_LEN + 2];
    if (!fits(snprintf(uniqueId, sizeof(uniqueId), "%s_%s", id.uniquePrefix, e.key), sizeof(uniqueId))) {
        return 0;
    }

    char stateTopic[HA_TOPIC_MAX];
    char availTopic[HA_TOPIC_MAX];
    char cmdTopic[HA_TOPIC_MAX];
    if (!buildStateTopic(id, e, stateTopic, sizeof(stateTopic)) ||
        !buildAvailabilityTopic(id, availTopic, sizeof(availTopic))) {
        return 0;
    }
    bool commandable = reg.isCommandable(i);
    if (commandable && !buildCommandTopic(id, e, cmdTopic, sizeof(cmdTopic))) {
        return 0;
    }

    JsonDocument doc;
    doc["name"] = nameBuf;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["availability_topic"] = availTopic;

    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray ids = device["identifiers"].to<JsonArray>();
    ids.add(id.uniquePrefix);
    device["name"] = id.deviceName;
    device["manufacturer"] = "heater-system";
    device["model"] = id.model;
    device["sw_version"] = fwVersion != nullptr ? fwVersion : "";

    switch (e.component) {
        case HaComponent::Number: {
            doc["command_topic"] = cmdTopic;
            const SettingDescriptor* d = config.descriptor(e.ref);
            if (d != nullptr) {
                if (d->type == SettingType::Int) {
                    doc["min"] = static_cast<int32_t>(d->minValue);
                    doc["max"] = static_cast<int32_t>(d->maxValue);
                    doc["step"] = static_cast<int32_t>(d->step);
                } else {
                    doc["min"] = d->minValue;
                    doc["max"] = d->maxValue;
                    doc["step"] = d->step;
                }
                if (d->unit != nullptr) {
                    doc["unit_of_measurement"] = d->unit;
                }
            }
            doc["mode"] = "box";
            doc["entity_category"] = "config";  // Number is always entity_category: config (D8)
            break;
        }
        case HaComponent::Switch: {
            doc["command_topic"] = cmdTopic;
            doc["payload_on"] = "ON";
            doc["payload_off"] = "OFF";
            if (e.configCategory) {
                doc["entity_category"] = "config";
            }
            break;
        }
        case HaComponent::BinarySensor: {
            doc["payload_on"] = "ON";
            doc["payload_off"] = "OFF";
            const char* deviceClass = nullptr;
            const char* entityCategory = nullptr;
            if (e.source == HaSource::Relay && reg.hw() != nullptr) {
                if (reg.hw()->relays[e.ref].role == RelayRole::Pump) {
                    deviceClass = "running";
                }
            } else if (e.source == HaSource::Custom && e.custom != nullptr) {
                deviceClass = e.custom->deviceClass;
                entityCategory = e.custom->entityCategory;
            }
            if (deviceClass != nullptr) {
                doc["device_class"] = deviceClass;
            }
            if (entityCategory != nullptr) {
                doc["entity_category"] = entityCategory;
            }
            break;
        }
        case HaComponent::Sensor: {
            const char* unit = nullptr;
            const char* deviceClass = nullptr;
            const char* stateClass = nullptr;
            const char* entityCategory = nullptr;
            bool tempPrecision = false;
            if (e.source == HaSource::LogicalSensor) {
                unit = "\xC2\xB0"
                       "C";  // "°C"
                deviceClass = "temperature";
                stateClass = "measurement";
                tempPrecision = true;
            } else if (e.source == HaSource::Custom && e.custom != nullptr) {
                unit = e.custom->unit;
                deviceClass = e.custom->deviceClass;
                stateClass = e.custom->stateClass;
                entityCategory = e.custom->entityCategory;
            }
            if (unit != nullptr) {
                doc["unit_of_measurement"] = unit;
            }
            if (deviceClass != nullptr) {
                doc["device_class"] = deviceClass;
            }
            if (stateClass != nullptr) {
                doc["state_class"] = stateClass;
            }
            if (tempPrecision) {
                doc["suggested_display_precision"] = 1;
            }
            if (entityCategory != nullptr) {
                doc["entity_category"] = entityCategory;
            }
            break;
        }
    }

    if (measureJson(doc) + 1 > cap) {
        return 0;
    }
    return serializeJson(doc, out, cap);
}

size_t buildEventPayload(const EventEntry& e, char* out, size_t cap) {
    JsonDocument doc;
    doc["seq"] = e.seq;
    doc["ts"] = e.timestamp;
    doc["rt"] = (e.flags & EVENT_FLAG_REAL_TIME) != 0;
    const char* key = eventTypeKey(e.type);
    if (key != nullptr) {
        doc["type"] = key;
    } else {
        doc["type"] = e.type;
    }
    doc["source"] = e.source;
    doc["value"] = e.value;
    doc["aux"] = e.aux;
    doc["reason"] = e.reason;

    if (measureJson(doc) + 1 > cap) {
        return 0;
    }
    return serializeJson(doc, out, cap);
}
