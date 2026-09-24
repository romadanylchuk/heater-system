#pragma once
#include <stddef.h>
#include "../../lib/CoreEngine/src/Command.h"

// Header-only single-thread CommandQueue fake for native tests: a fixed ring of
// FreeRtosCommandQueue::DEPTH (16) commands. Not shipped in firmware (test-only,
// common/test/fakes/).
class InMemoryCommandQueue : public CommandQueue {
public:
    static constexpr size_t DEPTH = 16;

    bool post(const Command& cmd) override {
        if (_count >= DEPTH) {
            return false;
        }
        _items[(_head + _count) % DEPTH] = cmd;
        ++_count;
        return true;
    }

    bool tryReceive(Command& out) override {
        if (_count == 0) {
            return false;
        }
        out = _items[_head];
        _head = (_head + 1) % DEPTH;
        --_count;
        return true;
    }

    size_t size() const { return _count; }

private:
    Command _items[DEPTH];
    size_t _head = 0;
    size_t _count = 0;
};
