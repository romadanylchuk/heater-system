#pragma once
#include <stddef.h>
#include <stdint.h>
#include <ConfigEngine.h>
#include <EventTypes.h>

// Pure MQTT connection-lifecycle decision machine (D6). It owns no transport:
// every tick() call returns the actions the caller (NetEsp32's
// AsyncMqttTransport adapter, driven by ConnectivityRuntime) must execute.
// Time is injected (uint64_t nowMs); no hardware, no AsyncMqtt/Arduino
// includes.
struct MqttEndpoint {
    char host[65];
    uint16_t port;
    char user[33];
    char pass[65];
};

// Reads mqttHost/Port/User/Pass by commonIndex. Returns true when enabled
// (host non-empty).
bool mqttEndpointFromConfig(const ConfigEngine& config, MqttEndpoint& out);

bool mqttEndpointEquals(const MqttEndpoint& a, const MqttEndpoint& b);

struct MqttSessionActions {
    bool disconnect = false;       // 1. station-style close of an in-progress/open connection
    bool forceDisconnect = false;  // 2. Wi-Fi went down while the transport reported connected
    bool configure = false;        // 3. push host/port/user/pass to the transport (disconnected only)
    bool connect = false;          // 4. non-blocking connect attempt
    bool sessionStarted = false;   // informational edges (effective connected)
    bool sessionEnded = false;
};

class MqttSession {
public:
    static constexpr uint32_t BACKOFF_FIRST_MS = 5000, BACKOFF_MAX_MS = 60000;

    // Stores the endpoint and arms configure-on-next-disconnected-tick.
    void begin(const MqttEndpoint& ep, uint64_t nowMs);

    MqttSessionActions tick(const MqttEndpoint& ep, bool wifiUp, bool transportConnected, uint64_t nowMs,
        EventSink& events);

    bool enabled() const { return _endpoint.host[0] != '\0'; }
    bool connected() const { return _effective; }  // effective = enabled && wifiUp && transport
    uint32_t connectCount() const { return _connectCount; }
    uint32_t backoffMs() const { return _backoff; }
    const MqttEndpoint& endpoint() const { return _endpoint; }

private:
    MqttEndpoint _endpoint{};

    bool _configurePending = false;
    uint32_t _backoff = BACKOFF_FIRST_MS;
    uint64_t _nextAttempt = 0;

    bool _pendingAttempt = false;  // an attempt was issued and has not yet succeeded
    bool _failureLogged = false;   // MqttDisconnected(2) already logged; re-armed only by a successful connect

    bool _effective = false;
    bool _prevWifiUp = false;
    uint32_t _connectCount = 0;
};
