#pragma once
#include <stddef.h>
#include <string>
#include <vector>
#include "../../lib/NetEngine/src/MqttTransport.h"

// Header-only MqttTransport fake for native tests: records every call
// (publishes/subscriptions/configure args/connect-disconnect counts), plus a
// switch to fail the next N publish() calls so tests can exercise the
// throttle/stall/retry contract (D10). Not shipped in firmware (test-only,
// common/test/fakes/).
class FakeMqttTransport : public MqttTransport {
public:
    struct Publish {
        std::string topic;
        std::string payload;
        bool retain;
    };

    std::vector<Publish> publishes;
    std::vector<std::string> subscriptions;

    bool configured = false;
    MqttEndpoint lastEndpoint{};
    std::string lastClientId;
    std::string lastWillTopic;

    int connectCount = 0;
    int disconnectCount = 0;
    bool lastDisconnectForce = false;
    bool connectedFlag = false;

    // Test-only: makes the next N publish() calls fail (return false,
    // nothing recorded) without touching connectedFlag/subscriptions.
    int failNextPublishes = 0;
    bool failAllPublishes = false;

    void configure(const MqttEndpoint& ep, const char* clientId, const char* willTopic) override {
        configured = true;
        lastEndpoint = ep;
        lastClientId = clientId != nullptr ? clientId : "";
        lastWillTopic = willTopic != nullptr ? willTopic : "";
    }

    void connect() override { ++connectCount; }

    // publishes.size() at each disconnect() call, so tests can assert what
    // was published before a disconnect (e.g. the retained "offline").
    std::vector<size_t> publishCountAtDisconnect;

    void disconnect(bool force) override {
        publishCountAtDisconnect.push_back(publishes.size());
        ++disconnectCount;
        lastDisconnectForce = force;
        connectedFlag = false;
    }

    bool connected() const override { return connectedFlag; }

    bool publish(const char* topic, const char* payload, bool retain) override {
        if (failAllPublishes || failNextPublishes > 0) {
            if (failNextPublishes > 0) {
                --failNextPublishes;
            }
            return false;
        }
        publishes.push_back(Publish{topic != nullptr ? topic : "", payload != nullptr ? payload : "", retain});
        return true;
    }

    bool subscribe(const char* topic) override {
        subscriptions.push_back(topic != nullptr ? topic : "");
        return true;
    }
};
