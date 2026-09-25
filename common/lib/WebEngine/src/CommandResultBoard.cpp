#include "CommandResultBoard.h"

void CommandResultBoard::expect(uint32_t id) {
    for (size_t i = 0; i < CAPACITY; ++i) {
        if (_slots[i].active && _slots[i].id == id) {
            _slots[i].state = CmdLookup::Pending;
            _slots[i].hasValue = false;
            return;
        }
    }
    Slot& s = _slots[_next];
    s.active = true;
    s.id = id;
    s.state = CmdLookup::Pending;
    s.status = CommandStatus::Ok;
    s.hasValue = false;
    s.value = 0.0f;
    _next = (_next + 1) % CAPACITY;
}

void CommandResultBoard::record(uint32_t id, CommandStatus st, bool hasValue, float value) {
    for (size_t i = 0; i < CAPACITY; ++i) {
        if (_slots[i].active && _slots[i].id == id) {
            _slots[i].state = CmdLookup::Done;
            _slots[i].status = st;
            _slots[i].hasValue = hasValue;
            _slots[i].value = value;
            return;
        }
    }
}

CmdLookup CommandResultBoard::lookup(uint32_t id, CmdResult& out) const {
    for (size_t i = 0; i < CAPACITY; ++i) {
        if (_slots[i].active && _slots[i].id == id) {
            out.id = id;
            out.status = _slots[i].status;
            out.hasValue = _slots[i].hasValue;
            out.value = _slots[i].value;
            return _slots[i].state;
        }
    }
    return CmdLookup::Unknown;
}
