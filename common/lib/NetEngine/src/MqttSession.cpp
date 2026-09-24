#include "MqttSession.h"
#include <algorithm>
#include <CommonSettings.h>
#include <string.h>

namespace {
void copyTrunc(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}
}  // namespace

bool mqttEndpointFromConfig(const ConfigEngine& config, MqttEndpoint& out) {
    copyTrunc(out.host, sizeof(out.host), config.getText(commonIndex(CommonSetting::MqttHost)));
    out.port = static_cast<uint16_t>(config.getInt(commonIndex(CommonSetting::MqttPort)));
    copyTrunc(out.user, sizeof(out.user), config.getText(commonIndex(CommonSetting::MqttUser)));
    copyTrunc(out.pass, sizeof(out.pass), config.getText(commonIndex(CommonSetting::MqttPass)));
    return out.host[0] != '\0';
}

bool mqttEndpointEquals(const MqttEndpoint& a, const MqttEndpoint& b) {
    return a.port == b.port && strcmp(a.host, b.host) == 0 && strcmp(a.user, b.user) == 0 &&
        strcmp(a.pass, b.pass) == 0;
}

void MqttSession::begin(const MqttEndpoint& ep, uint64_t nowMs) {
    _endpoint = ep;
    _configurePending = enabled();
    _backoff = BACKOFF_FIRST_MS;
    _nextAttempt = nowMs;
    _pendingAttempt = false;
    _failureLogged = false;
    _effective = false;
    _prevWifiUp = false;
    _connectCount = 0;
}

MqttSessionActions MqttSession::tick(const MqttEndpoint& ep, bool wifiUp, bool transportConnected, uint64_t nowMs,
    EventSink& events) {
    MqttSessionActions a{};

    // 1. Endpoint change vs the cache: close any open/pending connection, arm a
    // fresh configure+attempt, reset the backoff and re-arm the failure log.
    if (!mqttEndpointEquals(ep, _endpoint)) {
        _endpoint = ep;
        if (transportConnected) {
            a.disconnect = true;
        }
        _configurePending = true;
        _backoff = BACKOFF_FIRST_MS;
        _nextAttempt = nowMs;
        _failureLogged = false;
        _pendingAttempt = false;
    }

    const bool enabledNow = _endpoint.host[0] != '\0';

    // 2. Disabled: close if open, nothing else runs.
    if (!enabledNow) {
        if (transportConnected) {
            a.disconnect = true;
        }
        if (_effective) {
            a.sessionEnded = true;
            events.logEvent(toU16(EventType::MqttDisconnected), EVENT_SOURCE_MQTT, wifiUp ? 0.0f : 1.0f, 0.0f,
                EventReason::Logic);
        }
        _effective = false;
        _configurePending = false;
        _pendingAttempt = false;
        _prevWifiUp = wifiUp;
        return a;
    }

    // 3. Wi-Fi edges: a rising edge is an immediate, fresh attempt (backoff
    // reset); a falling edge clears any in-flight attempt bookkeeping so the
    // next session's first attempt is never mistaken for "a previous attempt
    // failed".
    if (wifiUp && !_prevWifiUp) {
        _backoff = BACKOFF_FIRST_MS;
        _nextAttempt = nowMs;
        _pendingAttempt = false;
    } else if (!wifiUp && _prevWifiUp) {
        _pendingAttempt = false;
    }

    // 4. Wi-Fi down while the transport still reports connected -> force close.
    if (!wifiUp && transportConnected) {
        a.forceDisconnect = true;
    }

    // 5. Configure: emitted once, on a tick where the transport reports
    // disconnected, immediately followed (same tick) by an attempt once
    // Wi-Fi is up.
    if (_configurePending && !transportConnected) {
        a.configure = true;
        _configurePending = false;
        _nextAttempt = nowMs;
    }

    // 6. Attempt: enabled && Wi-Fi up && transport not connected && due.
    if (wifiUp && !transportConnected && nowMs >= _nextAttempt) {
        if (_pendingAttempt && !_failureLogged) {
            events.logEvent(toU16(EventType::MqttDisconnected), EVENT_SOURCE_MQTT, 2.0f, 0.0f, EventReason::Logic);
            _failureLogged = true;
        }
        a.connect = true;
        _pendingAttempt = true;
        _nextAttempt = nowMs + _backoff;
        _backoff = std::min(_backoff * 2, BACKOFF_MAX_MS);
    }

    // 7. Effective (enabled && wifiUp && transport) edges.
    const bool effNow = wifiUp && transportConnected;
    if (effNow && !_effective) {
        ++_connectCount;
        a.sessionStarted = true;
        events.logEvent(toU16(EventType::MqttConnected), EVENT_SOURCE_MQTT, static_cast<float>(_connectCount), 0.0f,
            EventReason::Logic);
        _backoff = BACKOFF_FIRST_MS;
        _failureLogged = false;
        _pendingAttempt = false;
    } else if (!effNow && _effective) {
        a.sessionEnded = true;
        events.logEvent(toU16(EventType::MqttDisconnected), EVENT_SOURCE_MQTT, wifiUp ? 0.0f : 1.0f, 0.0f,
            EventReason::Logic);
    }
    _effective = effNow;
    _prevWifiUp = wifiUp;
    return a;
}
