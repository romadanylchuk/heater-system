#include "AsyncMqttTransport.h"
#include <MqttInbound.h>
#include <string.h>

namespace {

void copyText(char* dst, size_t cap, const char* src) {
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

}  // namespace

void AsyncMqttTransport::begin(const HaEntityRegistry* reg, const char* prefix, CommandQueue& queue,
    NetSignals& signals) {
    _registry = reg;
    _prefix = prefix;
    _queue = &queue;
    _signals = &signals;
    // Registered once at boot; the lambda only forwards to onMessage().
    _client.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties, size_t len,
                          size_t index, size_t total) { onMessage(topic, payload, len, index, total); });
}

void AsyncMqttTransport::onMessage(const char* topic, const char* payload, size_t len, size_t index,
    size_t total) {
    // AsyncTCP task. Only complete, single-packet, short payloads (D11).
    if (index != 0 || len != total || len > MQTT_CMD_PAYLOAD_MAX) {
        return;
    }
    if (_registry == nullptr || _prefix == nullptr || _queue == nullptr || _signals == nullptr ||
        topic == nullptr) {
        return;
    }
    handleMqttCommand(*_registry, _prefix, topic, payload, len, *_queue, *_signals);
}

void AsyncMqttTransport::configure(const MqttEndpoint& ep, const char* clientId, const char* willTopic) {
    copyText(_host, sizeof(_host), ep.host);
    copyText(_user, sizeof(_user), ep.user);
    copyText(_pass, sizeof(_pass), ep.pass);
    copyText(_clientId, sizeof(_clientId), clientId);
    copyText(_willTopic, sizeof(_willTopic), willTopic);

    _client.setServer(_host, ep.port);
    _client.setCredentials(_user[0] != '\0' ? _user : nullptr, _pass[0] != '\0' ? _pass : nullptr);
    _client.setClientId(_clientId);
    _client.setWill(_willTopic, 1, true, "offline");
    _client.setKeepAlive(KEEP_ALIVE_S);
    _client.setCleanSession(true);
}

void AsyncMqttTransport::connect() { _client.connect(); }

void AsyncMqttTransport::disconnect(bool force) { _client.disconnect(force); }

bool AsyncMqttTransport::connected() const { return _client.connected(); }

bool AsyncMqttTransport::publish(const char* topic, const char* payload, bool retain) {
    return _client.publish(topic, 0, retain, payload) != 0;
}

bool AsyncMqttTransport::subscribe(const char* topic) { return _client.subscribe(topic, 1) != 0; }
