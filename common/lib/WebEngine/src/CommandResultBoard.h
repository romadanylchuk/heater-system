#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Command.h"

// Per-command result board (D7): write endpoints post a command and answer
// 202 immediately; the SPA then polls GET /api/cmd?id=N. expect() is called
// at post time (AsyncTCP task, under WebServices' portMUX); record() is
// called from CoreRuntime's result hook on the loop task once the command
// has been applied (D7).
enum class CmdLookup : uint8_t { Unknown, Pending, Done };

struct CmdResult {
    uint32_t id = 0;
    CommandStatus status = CommandStatus::Ok;
    bool hasValue = false;
    float value = 0.0f;
};

class CommandResultBoard {
public:
    static constexpr size_t CAPACITY = 16;

    // Ring insert (overwrites the oldest slot when full), state Pending. An
    // id already on the board is reset to Pending in place (no new slot
    // consumed).
    void expect(uint32_t id);

    // No-op unless `id` is currently on the board (expect()'d, possibly
    // already Done from a stale duplicate).
    void record(uint32_t id, CommandStatus st, bool hasValue, float value);

    CmdLookup lookup(uint32_t id, CmdResult& out) const;

private:
    struct Slot {
        bool active = false;
        uint32_t id = 0;
        CmdLookup state = CmdLookup::Unknown;
        CommandStatus status = CommandStatus::Ok;
        bool hasValue = false;
        float value = 0.0f;
    };

    Slot _slots[CAPACITY];
    size_t _next = 0;   // ring cursor for the next fresh (never-seen-id) insert
};
