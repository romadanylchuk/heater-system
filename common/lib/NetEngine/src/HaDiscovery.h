#pragma once
#include <stddef.h>
#include <ConfigEngine.h>
#include "EventEntry.h"
#include "HaEntityRegistry.h"
#include "NetIdentity.h"

// Topic and JSON payload builders for Home Assistant MQTT discovery, state/
// command topics, availability and events (D7/D8/D9/D20). Pure formatting
// only: no MQTT client, no publish -- MqttPublisher (this phase) drives
// MqttTransport with what these functions build. ArduinoJson (already a
// project dependency, used the same way as BackupCodec) lives in the .cpp
// only.
constexpr const char* HA_DISCOVERY_PREFIX = "homeassistant";
constexpr size_t HA_TOPIC_MAX = 128;
constexpr size_t HA_DISCOVERY_PAYLOAD_MAX = 1024;
constexpr size_t MQTT_EVENT_PAYLOAD_MAX = 192;

// "sensor" / "binary_sensor" / "switch" / "number".
const char* haComponentName(HaComponent c);

// false on overflow (out left untouched other than a partial write attempt).
bool buildStateTopic(const NetIdentity& id, const HaEntity& e, char* out, size_t cap);        // <prefix>/<key>/state
bool buildCommandTopic(const NetIdentity& id, const HaEntity& e, char* out, size_t cap);       // <prefix>/<key>/set
bool buildDiscoveryTopic(const NetIdentity& id, const HaEntity& e, char* out, size_t cap);     // homeassistant/<comp>/<prefix>/<key>/config
bool buildAvailabilityTopic(const NetIdentity& id, char* out, size_t cap);                     // <prefix>/status
bool buildEventTopic(const NetIdentity& id, char* out, size_t cap);                            // <prefix>/event
bool buildCommandSubscription(const NetIdentity& id, char* out, size_t cap);                   // <prefix>/+/set

// Builds the HA discovery config JSON for entity i (D8): name, unique_id
// (<uniquePrefix>_<key>), state_topic, availability_topic, device{identifiers
// [uniquePrefix], name, manufacturer "heater-system", model, sw_version}, plus
// component-specific fields (Number: command_topic/min/max/step/mode "box"/
// unit_of_measurement?/entity_category "config"; Switch: command_topic/
// payload_on/payload_off/entity_category "config" if configCategory;
// BinarySensor: payload_on/payload_off/device_class ("running" for
// RelayRole::Pump, or the custom entity's device_class); Sensor: unit/
// device_class/state_class/suggested_display_precision (1 for logical
// sensors)/entity_category). Returns 0 on overflow (nothing written).
size_t buildDiscoveryPayload(const HaEntityRegistry& reg, size_t i, const ConfigEngine& config,
    const NetIdentity& id, const char* fwVersion, char* out, size_t cap);

// {"seq","ts","rt","type" (eventTypeKey() or the numeric type),"source",
// "value","aux","reason"}. Returns 0 on overflow.
size_t buildEventPayload(const EventEntry& e, char* out, size_t cap);
