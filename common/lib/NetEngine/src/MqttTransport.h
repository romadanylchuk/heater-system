#pragma once
#include <stddef.h>
#include "MqttSession.h"

// Port interface (D1/D2) the pure connectivity layer drives; NetEsp32's
// AsyncMqttTransport (a later phase) implements it over AsyncMqttClient. Only
// MqttEndpoint (MqttSession.h, itself pure) is pulled in -- no AsyncMqtt/
// Arduino includes here (D22).
class MqttTransport {
public:
    virtual ~MqttTransport() = default;

    // Copies every string into transport-owned storage (AsyncMqttClient keeps
    // raw pointers). Will: willTopic, payload "offline", retained, qos 1.
    // Only called while disconnected.
    virtual void configure(const MqttEndpoint& ep, const char* clientId, const char* willTopic) = 0;

    virtual void connect() = 0;  // non-blocking
    virtual void disconnect(bool force) = 0;
    virtual bool connected() const = 0;

    // false = not queued (e.g. the TCP send buffer lacks space); the caller
    // retries the same item next tick (D10).
    virtual bool publish(const char* topic, const char* payload, bool retain) = 0;

    virtual bool subscribe(const char* topic) = 0;
};
