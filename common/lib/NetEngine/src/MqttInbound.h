#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Command.h>
#include "HaEntityRegistry.h"
#include "NetSignals.h"

// Parses inbound `<prefix>/<key>/set` MQTT command payloads against the HA
// entity registry and posts them to the command queue (D11). Pure: no
// AsyncMqtt/network includes. Called from AsyncMqttTransport's message
// callback (AsyncTCP task in firmware, so it must never block and never
// allocate) and directly from native tests.
enum class InboundResult : uint8_t { Posted, UnknownTopic, NotCommandable, BadPayload, QueueFull };

constexpr size_t MQTT_CMD_PAYLOAD_MAX = 32;

// topic must be exactly "<prefix>/<key>/set". On UnknownTopic/NotCommandable,
// entity/value are left untouched. On Posted/BadPayload, entity is the known,
// commandable registry index (value is only meaningful on Posted).
InboundResult parseMqttCommand(const HaEntityRegistry& reg, const char* prefix, const char* topic,
    const char* payload, size_t len, size_t& entity, float& value);

// parseMqttCommand() + posts makeSetNumber(registry.entity(entity).ref, value,
// EventReason::Mqtt, 0) to the queue. Always sets signals.republish for a
// known commandable entity (Posted/BadPayload/QueueFull), so HA gets the real
// value echoed back even when the command is rejected/clamped/dropped.
// QueueFull also increments signals.droppedCommands. Never blocks, no heap.
InboundResult handleMqttCommand(const HaEntityRegistry& reg, const char* prefix, const char* topic,
    const char* payload, size_t len, CommandQueue& queue, NetSignals& signals);
