#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include "EventEntry.h"
#include "HaEntityRegistry.h"
#include "MqttTransport.h"
#include "NetIdentity.h"

// Throttled MQTT publish pipeline (D10): drives discovery -> availability ->
// states -> subscribe -> live changes/events, at most MAX_PER_PUMP publishes
// per pump() call, so the loop task never blocks on a full TCP send buffer.
// pump() stops (without advancing past the failing item) at the first
// MqttTransport::publish()==false, so the caller simply calls pump() again
// next fast tick. The caller (ConnectivityRuntime, a later phase) is
// responsible for not calling pump() while an OTA is active (D10) and for
// calling refresh() once before the pipeline reaches the States phase.
class MqttPublisher {
public:
    enum class Phase : uint8_t { Idle, Discovery, Availability, States, Subscribe, Live };

    static constexpr size_t MAX_PER_PUMP = 4;
    static constexpr size_t EVENT_OUTBOX = 8;
    static constexpr uint32_t REFRESH_MS = 60000;

    // Binds the (already-built) registry/identity/firmware version. Call once
    // at startup, before any onSessionStart().
    void begin(const HaEntityRegistry& reg, const NetIdentity& id, const char* fwVersion);

    void onSessionStart(uint64_t nowMs);  // Phase::Discovery, cursor 0, outbox cleared
    void onSessionEnd();                  // Phase::Idle, outbox cleared

    bool sessionActive() const { return _phase != Phase::Idle; }

    // Recomputes every entity's current formatted state; entities whose text
    // changed become dirty, and every entity becomes dirty again once every
    // REFRESH_MS (D9). Must run at least once before pump() reaches States.
    void refresh(const CommonState& s, const ConfigEngine& c, uint64_t nowMs);

    void markDirty(size_t entity);

    // Enqueues a non-retained event (D20); ignored unless sessionActive().
    // Drops the oldest queued event when the outbox is full.
    void enqueueEvent(const EventEntry& e);

    // Up to MAX_PER_PUMP publishes this call; returns the number sent.
    size_t pump(MqttTransport& t, const ConfigEngine& c);

    // Retained "offline" to the availability topic, ahead of a voluntary
    // disconnect (the LWT only covers an unexpected drop).
    bool publishOffline(MqttTransport& t);

    Phase phase() const { return _phase; }
    size_t cursor() const { return _cursor; }

private:
    void popOldestEvent();

    const HaEntityRegistry* _reg = nullptr;
    NetIdentity _id{};
    char _fwVersion[VERSION_TEXT_LEN + 1] = {};

    Phase _phase = Phase::Idle;
    size_t _cursor = 0;

    char _current[HA_MAX_ENTITIES][HA_STATE_MAX_LEN + 1] = {};
    char _last[HA_MAX_ENTITIES][HA_STATE_MAX_LEN + 1] = {};
    bool _dirty[HA_MAX_ENTITIES] = {};
    uint64_t _lastFullRefresh = 0;

    EventEntry _outbox[EVENT_OUTBOX] = {};
    size_t _outboxHead = 0;
    size_t _outboxCount = 0;
};
