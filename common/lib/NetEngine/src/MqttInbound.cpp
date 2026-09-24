#include "MqttInbound.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

bool ciEqual(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

}  // namespace

InboundResult parseMqttCommand(const HaEntityRegistry& reg, const char* prefix, const char* topic,
    const char* payload, size_t len, size_t& entity, float& value) {
    if (!prefix || !topic) {
        return InboundResult::UnknownTopic;
    }

    size_t prefixLen = strlen(prefix);
    if (strncmp(topic, prefix, prefixLen) != 0 || topic[prefixLen] != '/') {
        return InboundResult::UnknownTopic;
    }
    const char* rest = topic + prefixLen + 1;  // "<key>/set"
    size_t restLen = strlen(rest);
    if (restLen < 5 || strcmp(rest + restLen - 4, "/set") != 0) {  // "x" + "/set" = 5 min
        return InboundResult::UnknownTopic;
    }
    const char* key = rest;
    size_t keyLen = restLen - 4;

    int idx = reg.findByKey(key, keyLen);
    if (idx < 0) {
        return InboundResult::UnknownTopic;
    }
    if (!reg.isCommandable(static_cast<size_t>(idx))) {
        return InboundResult::NotCommandable;
    }
    entity = static_cast<size_t>(idx);

    if (len == 0 || len > MQTT_CMD_PAYLOAD_MAX || !payload) {
        return InboundResult::BadPayload;
    }
    char buf[MQTT_CMD_PAYLOAD_MAX + 1];
    memcpy(buf, payload, len);
    buf[len] = '\0';

    char* start = buf;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    char* end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    *end = '\0';
    if (start == end) {
        return InboundResult::BadPayload;
    }

    if (reg.entity(entity).component == HaComponent::Number) {
        char* strEnd = nullptr;
        float v = strtof(start, &strEnd);
        if (strEnd != end || !isfinite(v)) {
            return InboundResult::BadPayload;
        }
        value = v;
        return InboundResult::Posted;
    }

    // Switch.
    if (ciEqual(start, "on") || ciEqual(start, "1") || ciEqual(start, "true")) {
        value = 1.0f;
        return InboundResult::Posted;
    }
    if (ciEqual(start, "off") || ciEqual(start, "0") || ciEqual(start, "false")) {
        value = 0.0f;
        return InboundResult::Posted;
    }
    return InboundResult::BadPayload;
}

InboundResult handleMqttCommand(const HaEntityRegistry& reg, const char* prefix, const char* topic,
    const char* payload, size_t len, CommandQueue& queue, NetSignals& signals) {
    size_t entity = 0;
    float value = 0.0f;
    InboundResult r = parseMqttCommand(reg, prefix, topic, payload, len, entity, value);
    if (r == InboundResult::UnknownTopic || r == InboundResult::NotCommandable) {
        return r;
    }

    // Known, commandable entity: always echo the real value back to HA, even
    // if the payload/clamp/queue outcome below is not a success (D11).
    signals.republish.set(entity);

    if (r == InboundResult::BadPayload) {
        return r;
    }

    Command cmd = makeSetNumber(reg.entity(entity).ref, value, EventReason::Mqtt, 0);
    if (!queue.post(cmd)) {
        signals.droppedCommands.fetch_add(1, std::memory_order_relaxed);
        return InboundResult::QueueFull;
    }
    return InboundResult::Posted;
}
