#include "SharedJsonBuffer.h"
#include <ESPAsyncWebServer.h>
#include <string.h>

SharedJsonBuffer::SharedJsonBuffer(char* buf, size_t cap)
    : _mutex(xSemaphoreCreateMutexStatic(&_mutexBuf)), _buf(buf), _cap(cap) {}

bool SharedJsonBuffer::update(const char* json, size_t len) {
    if (json == nullptr || _buf == nullptr || len == 0 || len >= _cap) {
        return false;  // builder overflowed or oversized: keep the old document
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(UPDATE_WAIT_MS)) != pdTRUE) {
        return false;
    }
    memcpy(_buf, json, len);
    _buf[len] = '\0';
    _len = len;
    xSemaphoreGive(_mutex);
    return true;
}

void SharedJsonBuffer::send(AsyncWebServerRequest* r) const {
    if (r == nullptr) {
        return;
    }
    AsyncResponseStream* resp = nullptr;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(SEND_WAIT_MS)) == pdTRUE) {
        size_t len = _len;
        if (len > 0) {
            resp = r->beginResponseStream("application/json", len + 1);
            if (resp != nullptr && resp->write(reinterpret_cast<const uint8_t*>(_buf), len) != len) {
                delete resp;  // out of heap: never send a truncated document
                resp = nullptr;
            }
        }
        xSemaphoreGive(_mutex);
    }
    if (resp == nullptr) {
        r->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
        return;
    }
    resp->addHeader("Cache-Control", "no-store");
    r->send(resp);
}
