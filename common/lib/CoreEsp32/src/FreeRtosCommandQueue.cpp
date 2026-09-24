#include "FreeRtosCommandQueue.h"

bool FreeRtosCommandQueue::begin() {
    _queue = xQueueCreate(DEPTH, sizeof(Command));
    return _queue != nullptr;
}

bool FreeRtosCommandQueue::post(const Command& cmd) {
    if (_queue == nullptr) return false;
    return xQueueSend(_queue, &cmd, 0) == pdTRUE;
}

bool FreeRtosCommandQueue::tryReceive(Command& out) {
    if (_queue == nullptr) return false;
    return xQueueReceive(_queue, &out, 0) == pdTRUE;
}
