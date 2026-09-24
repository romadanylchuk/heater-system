#pragma once
#include <AsyncMqttClient.h>
#include <Command.h>
#include <HaEntityRegistry.h>
#include <MqttTransport.h>
#include <NetSignals.h>

// MqttTransport over AsyncMqttClient (D6/D10/D11).
//
// Threading: AsyncMqttClient's callbacks (onMessage here) run on the AsyncTCP
// task. The message callback touches ONLY the immutable HaEntityRegistry, the
// thread-safe CommandQueue (non-blocking post) and the NetSignals atomics via
// handleMqttCommand() -- never CommonState/ConfigEngine, no Serial, no heap.
// Every other method is called from the loop task by ConnectivityRuntime.
//
// Packet size: publish() uses QoS 0 and fails fast (returns false) when the
// lwIP TCP send buffer lacks space (AsyncClient::space() < packet size), which
// MqttPublisher treats as back-pressure and retries next fast tick. The send
// buffer is CONFIG_LWIP_TCP_SND_BUF_DEFAULT = 5760 B in the Arduino-ESP32
// 2.0.17 sdkconfig, so the largest packet (HA_DISCOVERY_PAYLOAD_MAX = 1024 B
// payload + topic + 5 B header) always fits into an empty buffer; no
// AsyncMqttClient/lwIP configuration change is needed. AsyncMqttClient
// serialises publish() with an internal semaphore (max 1000 ms wait) shared
// with its AsyncTCP-task callbacks; contention is limited to those short
// callbacks.
class AsyncMqttTransport : public MqttTransport {
public:
    static constexpr uint16_t KEEP_ALIVE_S = 30;
    static constexpr size_t CLIENT_ID_MAX = 32;
    static constexpr size_t WILL_TOPIC_MAX = 96;

    // Registers the inbound message callback. reg/prefix must outlive the
    // transport (the registry is owned by ConnectivityRuntime and immutable
    // after its begin(); prefix is the static NetIdentity string). A null
    // registry or prefix leaves inbound commands ignored.
    void begin(const HaEntityRegistry* reg, const char* prefix, CommandQueue& queue, NetSignals& signals);

    void configure(const MqttEndpoint& ep, const char* clientId, const char* willTopic) override;
    void connect() override;
    void disconnect(bool force) override;
    bool connected() const override;
    bool publish(const char* topic, const char* payload, bool retain) override;
    bool subscribe(const char* topic) override;

private:
    void onMessage(const char* topic, const char* payload, size_t len, size_t index, size_t total);

    AsyncMqttClient _client;

    // AsyncMqttClient stores raw pointers: every string lives here.
    char _host[sizeof(MqttEndpoint::host)] = {};
    char _user[sizeof(MqttEndpoint::user)] = {};
    char _pass[sizeof(MqttEndpoint::pass)] = {};
    char _clientId[CLIENT_ID_MAX + 1] = {};
    char _willTopic[WILL_TOPIC_MAX + 1] = {};

    const HaEntityRegistry* _registry = nullptr;
    const char* _prefix = nullptr;
    CommandQueue* _queue = nullptr;
    NetSignals* _signals = nullptr;
};
