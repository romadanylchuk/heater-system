#include "MqttPublisher.h"

#include <stdio.h>
#include <string.h>
#include "HaDiscovery.h"

void MqttPublisher::begin(const HaEntityRegistry& reg, const NetIdentity& id, const char* fwVersion) {
    _reg = &reg;
    _id = id;
    snprintf(_fwVersion, sizeof(_fwVersion), "%s", fwVersion != nullptr ? fwVersion : "");

    _phase = Phase::Idle;
    _cursor = 0;
    for (size_t i = 0; i < HA_MAX_ENTITIES; ++i) {
        _current[i][0] = '\0';
        _last[i][0] = '\0';
        _dirty[i] = false;
    }
    _lastFullRefresh = 0;
    _outboxHead = 0;
    _outboxCount = 0;
}

void MqttPublisher::onSessionStart(uint64_t nowMs) {
    (void)nowMs;
    _phase = Phase::Discovery;
    _cursor = 0;
    _outboxHead = 0;
    _outboxCount = 0;
}

void MqttPublisher::onSessionEnd() {
    _phase = Phase::Idle;
    _cursor = 0;
    _outboxHead = 0;
    _outboxCount = 0;
}

void MqttPublisher::refresh(const CommonState& s, const ConfigEngine& c, uint64_t nowMs) {
    if (_reg == nullptr) {
        return;
    }
    bool fullRefresh = (nowMs - _lastFullRefresh) >= REFRESH_MS;
    if (fullRefresh) {
        _lastFullRefresh = nowMs;
    }
    for (size_t i = 0; i < _reg->count(); ++i) {
        char buf[HA_STATE_MAX_LEN + 1];
        _reg->formatState(i, s, c, buf, sizeof(buf));
        bool changed = strcmp(buf, _current[i]) != 0;
        if (changed) {
            memcpy(_current[i], buf, sizeof(buf));
        }
        if (changed || fullRefresh) {
            _dirty[i] = true;
        }
    }
}

void MqttPublisher::markDirty(size_t entity) {
    if (_reg != nullptr && entity < _reg->count()) {
        _dirty[entity] = true;
    }
}

void MqttPublisher::popOldestEvent() {
    _outboxHead = (_outboxHead + 1) % EVENT_OUTBOX;
    --_outboxCount;
}

void MqttPublisher::enqueueEvent(const EventEntry& e) {
    if (!sessionActive()) {
        return;
    }
    if (_outboxCount == EVENT_OUTBOX) {
        popOldestEvent();  // drop the oldest queued event
    }
    size_t tail = (_outboxHead + _outboxCount) % EVENT_OUTBOX;
    _outbox[tail] = e;
    ++_outboxCount;
}

bool MqttPublisher::publishOffline(MqttTransport& t) {
    char topic[HA_TOPIC_MAX];
    if (!buildAvailabilityTopic(_id, topic, sizeof(topic))) {
        return false;
    }
    return t.publish(topic, "offline", true);
}

size_t MqttPublisher::pump(MqttTransport& t, const ConfigEngine& c) {
    if (_reg == nullptr) {
        return 0;
    }
    size_t sent = 0;
    while (sent < MAX_PER_PUMP) {
        switch (_phase) {
            case Phase::Idle:
                return sent;

            case Phase::Discovery: {
                if (_cursor >= _reg->count()) {
                    _phase = Phase::Availability;
                    break;
                }
                char topic[HA_TOPIC_MAX];
                char payload[HA_DISCOVERY_PAYLOAD_MAX];
                bool topicOk = buildDiscoveryTopic(_id, _reg->entity(_cursor), topic, sizeof(topic));
                size_t len = topicOk
                    ? buildDiscoveryPayload(*_reg, _cursor, c, _id, _fwVersion, payload, sizeof(payload))
                    : 0;
                if (!topicOk || len == 0) {
                    ++_cursor;  // unbuildable: skip, the pipeline never stalls (D10)
                    break;
                }
                if (!t.publish(topic, payload, true)) {
                    return sent;  // TCP send buffer full: retry this entity next tick
                }
                ++_cursor;
                ++sent;
                break;
            }

            case Phase::Availability: {
                char topic[HA_TOPIC_MAX];
                if (!buildAvailabilityTopic(_id, topic, sizeof(topic))) {
                    _phase = Phase::States;
                    _cursor = 0;
                    break;
                }
                if (!t.publish(topic, "online", true)) {
                    return sent;
                }
                _phase = Phase::States;
                _cursor = 0;
                ++sent;
                break;
            }

            case Phase::States: {
                if (_cursor >= _reg->count()) {
                    _phase = Phase::Subscribe;
                    break;
                }
                char topic[HA_TOPIC_MAX];
                if (!buildStateTopic(_id, _reg->entity(_cursor), topic, sizeof(topic))) {
                    ++_cursor;
                    break;
                }
                if (!t.publish(topic, _current[_cursor], true)) {
                    return sent;
                }
                memcpy(_last[_cursor], _current[_cursor], sizeof(_last[_cursor]));
                _dirty[_cursor] = false;
                ++_cursor;
                ++sent;
                break;
            }

            case Phase::Subscribe: {
                char topic[HA_TOPIC_MAX];
                if (!buildCommandSubscription(_id, topic, sizeof(topic))) {
                    _phase = Phase::Live;
                    _cursor = 0;
                    break;
                }
                if (!t.subscribe(topic)) {
                    return sent;
                }
                _phase = Phase::Live;
                _cursor = 0;
                ++sent;
                break;
            }

            case Phase::Live: {
                // Dirty entities first (D10). Rescanned from 0 every call: a
                // publish failure leaves the entity dirty, so it is found and
                // retried again without needing a persisted scan position.
                size_t i = 0;
                while (i < _reg->count() && !_dirty[i]) {
                    ++i;
                }
                if (i < _reg->count()) {
                    char topic[HA_TOPIC_MAX];
                    if (!buildStateTopic(_id, _reg->entity(i), topic, sizeof(topic))) {
                        _dirty[i] = false;  // unbuildable: skip, never stalls
                        break;
                    }
                    if (!t.publish(topic, _current[i], true)) {
                        return sent;
                    }
                    memcpy(_last[i], _current[i], sizeof(_last[i]));
                    _dirty[i] = false;
                    ++sent;
                    break;
                }
                // No dirty entity: try the event outbox (oldest first).
                if (_outboxCount > 0) {
                    char topic[HA_TOPIC_MAX];
                    char payload[MQTT_EVENT_PAYLOAD_MAX];
                    if (!buildEventTopic(_id, topic, sizeof(topic))) {
                        popOldestEvent();  // unbuildable: drop, never stalls
                        break;
                    }
                    size_t len = buildEventPayload(_outbox[_outboxHead], payload, sizeof(payload));
                    if (len == 0) {
                        popOldestEvent();
                        break;
                    }
                    if (!t.publish(topic, payload, false)) {
                        return sent;
                    }
                    popOldestEvent();
                    ++sent;
                    break;
                }
                return sent;  // nothing dirty and nothing queued this tick
            }
        }
    }
    return sent;
}
