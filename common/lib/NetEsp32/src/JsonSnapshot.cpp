#include "JsonSnapshot.h"
#include <string.h>

JsonSnapshot::JsonSnapshot() : _mutex(xSemaphoreCreateMutexStatic(&_mutexBuf)) {}

void JsonSnapshot::update(const char* json) {
    if (json == nullptr) {
        return;
    }
    size_t len = strnlen(json, JSON_SNAPSHOT_CAP);
    if (len == 0 || len >= JSON_SNAPSHOT_CAP) {
        return;  // builder overflowed (returned "") or oversized: keep the old snapshot
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(UPDATE_WAIT_MS)) != pdTRUE) {
        return;
    }
    memcpy(_buf, json, len);
    _buf[len] = '\0';
    _len = len;
    xSemaphoreGive(_mutex);
}

size_t JsonSnapshot::copy(char* out, size_t cap) const {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(COPY_WAIT_MS)) != pdTRUE) {
        return 0;
    }
    size_t len = _len;
    if (len == 0 || len + 1 > cap) {
        len = 0;
    } else {
        memcpy(out, _buf, len + 1);
    }
    xSemaphoreGive(_mutex);
    return len;
}
