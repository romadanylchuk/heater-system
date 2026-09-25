#include "ExportSlot.h"
#include <stdlib.h>

bool ExportSlot::request() {
    if (_state == ExportState::Idle) {
        _state = ExportState::Requested;
    }
    return true;
}

void ExportSlot::publish(char* data, size_t len, uint64_t nowMs) {
    if (_state == ExportState::Ready && _data != nullptr) {
        free(_data);
        _data = nullptr;
    }
    if (data == nullptr) {
        _state = ExportState::Idle;
        _data = nullptr;
        _len = 0;
        return;
    }
    _data = data;
    _len = len;
    _readyAtMs = nowMs;
    _state = ExportState::Ready;
}

bool ExportSlot::take(char*& data, size_t& len) {
    if (_state != ExportState::Ready) {
        return false;
    }
    data = _data;
    len = _len;
    _data = nullptr;
    _len = 0;
    _state = ExportState::Idle;
    return true;
}

char* ExportSlot::expire(uint64_t nowMs) {
    if (_state == ExportState::Ready && nowMs - _readyAtMs >= READY_TTL_MS) {
        char* d = _data;
        _data = nullptr;
        _len = 0;
        _state = ExportState::Idle;
        return d;
    }
    return nullptr;
}
