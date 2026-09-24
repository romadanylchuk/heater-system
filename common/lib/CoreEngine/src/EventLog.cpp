#include "EventLog.h"

#include <stdio.h>

EventLog::EventLog(KvStore& store, const Clock& clock) : _store(store), _clock(clock) {}

void EventLog::slotKey(size_t slot, char out[6]) {
    snprintf(out, 6, "ev%02zu", slot);
}

void EventLog::push(const EventEntry& e) {
    _entries[_head] = e;
    _head = (_head + 1) % CAPACITY;
    if (_count < CAPACITY) {
        ++_count;
    }
}

const EventEntry* EventLog::newest(size_t indexFromNewest) const {
    if (indexFromNewest >= _count) {
        return nullptr;
    }
    size_t idx = (_head + CAPACITY - 1 - indexFromNewest) % CAPACITY;
    return &_entries[idx];
}

bool EventLog::begin() {
    bool storeOk = true;
    EventEntry valid[CAPACITY];
    size_t validCount = 0;

    for (size_t i = 0; i < CAPACITY; ++i) {
        char key[6];
        slotKey(i, key);
        EventEntry e{};
        StoreStatus st = _store.getBlob(key, &e, sizeof(EventEntry));
        if (st == StoreStatus::NotFound) {
            continue;
        }
        if (st != StoreStatus::Ok) {
            if (st != StoreStatus::TooLarge && st != StoreStatus::TypeMismatch) {
                storeOk = false;
            }
            continue;
        }
        if (!isEventEntryValid(e)) {
            continue;
        }
        valid[validCount++] = e;
    }

    // Insertion sort by seq ascending; validCount <= CAPACITY (50), so O(n^2) is fine.
    for (size_t i = 1; i < validCount; ++i) {
        EventEntry key = valid[i];
        size_t j = i;
        while (j > 0 && valid[j - 1].seq > key.seq) {
            valid[j] = valid[j - 1];
            --j;
        }
        valid[j] = key;
    }

    _head = 0;
    _count = 0;
    uint32_t maxSeq = 0;
    for (size_t i = 0; i < validCount; ++i) {
        push(valid[i]);
        if (valid[i].seq > maxSeq) {
            maxSeq = valid[i].seq;
        }
    }
    _nextSeq = maxSeq + 1;

    return storeOk;
}

bool EventLog::logEvent(uint16_t type, uint16_t source, float value, float aux, EventReason reason) {
    uint64_t mono = _clock.monoMs();
    if (!_limiter.allow(type, source, mono)) {
        _lastStatus = EventLogStatus::RateLimited;
        return false;
    }

    EventEntry e{};
    e.seq = _nextSeq++;
    Timestamp ts = _clock.now();
    e.timestamp = ts.seconds;
    e.type = type;
    e.source = source;
    e.value = value;
    e.aux = aux;
    e.reason = static_cast<uint8_t>(reason);
    e.flags = ts.realTime ? EVENT_FLAG_REAL_TIME : 0;
    sealEventEntry(e);

    push(e);

    char key[6];
    slotKey(e.seq % CAPACITY, key);
    StoreStatus st = _store.setBlob(key, &e, sizeof(EventEntry));
    if (st == StoreStatus::Ok) {
        st = _store.commit();
    }
    if (st == StoreStatus::Ok) {
        _lastPersistOk = true;
        _lastStatus = EventLogStatus::Accepted;
    } else {
        _lastPersistOk = false;
        _lastStatus = EventLogStatus::PersistFailed;
    }

    if (_hook) {
        _hook(e, _hookCtx);
    }
    return true;
}
