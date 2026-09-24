#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../lib/CoreEngine/src/EventTypes.h"

// Header-only EventSink fake for native HwEngine tests: records every logged
// event (no rate limiting, logEvent() always returns true) so tests can assert
// the *attempted* logs regardless of EventLog's 60 s per-(type, source)
// suppression window (D8). Test-only, not shipped in firmware.
class RecordingEventSink : public EventSink {
public:
    struct Record {
        uint16_t type;
        uint16_t source;
        float value;
        float aux;
        EventReason reason;
    };

    static constexpr size_t CAPACITY = 256;

    bool logEvent(uint16_t type, uint16_t source, float value, float aux, EventReason reason) override {
        if (_count < CAPACITY) {
            _records[_count] = Record{type, source, value, aux, reason};
            ++_count;
        }
        return true;
    }

    size_t count() const { return _count; }

    const Record& at(size_t i) const { return _records[i]; }

    size_t countOf(EventType type) const {
        size_t n = 0;
        for (size_t i = 0; i < _count; ++i) {
            if (_records[i].type == toU16(type)) {
                ++n;
            }
        }
        return n;
    }

    size_t countOf(EventType type, uint16_t source) const {
        size_t n = 0;
        for (size_t i = 0; i < _count; ++i) {
            if (_records[i].type == toU16(type) && _records[i].source == source) {
                ++n;
            }
        }
        return n;
    }

    void clear() { _count = 0; }

private:
    Record _records[CAPACITY] = {};
    size_t _count = 0;
};
