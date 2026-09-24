#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Clock.h"
#include "EventEntry.h"
#include "EventRateLimiter.h"
#include "EventTypes.h"
#include "KvStore.h"

// Persisted, rate-limited event log: a 50-entry RAM ring backed by one NVS blob per
// slot ("ev00".."ev49"). Each accepted event is written and committed immediately
// (D12). A torn/corrupt slot only loses that one entry (D10).
enum class EventLogStatus : uint8_t { Accepted, RateLimited, PersistFailed };

using EventPublishHook = void (*)(const EventEntry& entry, void* ctx);

class EventLog : public EventSink {
public:
    static constexpr size_t CAPACITY = 50;

    EventLog(KvStore& store, const Clock& clock);

    // Loads slots from the store into the RAM ring. Returns false if a store read
    // failed for a reason other than NotFound/TooLarge/TypeMismatch; the log is
    // still usable (RAM ring reflects whatever loaded successfully).
    bool begin();

    bool logEvent(uint16_t type, uint16_t source, float value, float aux, EventReason reason) override;

    EventLogStatus lastStatus() const { return _lastStatus; }
    bool lastPersistOk() const { return _lastPersistOk; }
    size_t count() const { return _count; }

    // indexFromNewest == 0 is the latest entry; nullptr if out of range (>= count()).
    const EventEntry* newest(size_t indexFromNewest) const;

    uint32_t nextSeq() const { return _nextSeq; }

    void setPublishHook(EventPublishHook hook, void* ctx) {
        _hook = hook;
        _hookCtx = ctx;
    }

    // Formats "ev00".."ev49" into out (out must have room for 6 bytes incl. NUL).
    static void slotKey(size_t slot, char out[6]);

private:
    void push(const EventEntry& e);

    KvStore& _store;
    const Clock& _clock;
    EventRateLimiter _limiter;

    EventEntry _entries[CAPACITY]{};
    size_t _head = 0;    // index the next push() writes to
    size_t _count = 0;   // number of valid entries currently held (<= CAPACITY)
    uint32_t _nextSeq = 1;

    EventLogStatus _lastStatus = EventLogStatus::Accepted;
    bool _lastPersistOk = true;

    EventPublishHook _hook = nullptr;
    void* _hookCtx = nullptr;
};
