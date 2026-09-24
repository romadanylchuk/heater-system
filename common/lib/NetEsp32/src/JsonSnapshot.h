#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>

// Mutex-guarded JSON text buffer (D18): the loop task rebuilds it every slow
// tick from CommonState via NetJson, and web handlers on the AsyncTCP task
// copy it out, so no handler ever reads CommonState. Neither side blocks for
// long: update() gives up after 10 ms (the snapshot is simply one tick
// stale), copy() after 50 ms (the handler answers 503).
//
// JSON_SNAPSHOT_CAP (1536) covers the worst-case Wi-Fi status document (~312
// B incl. NUL, review-6: must be >= 320), the 16-entry scan list and the
// version document.
constexpr size_t JSON_SNAPSHOT_CAP = 1536;

class JsonSnapshot {
public:
    // Static mutex: no heap, safe to construct at static-init time.
    JsonSnapshot();

    // Loop task. Copies `json` (truncated never: text longer than the buffer
    // is rejected and the old snapshot kept). Skipped if the mutex is busy
    // for more than UPDATE_WAIT_MS.
    void update(const char* json);

    // Any task. Copies the snapshot (NUL-terminated) into out; returns its
    // length, or 0 on timeout / empty snapshot / insufficient cap.
    size_t copy(char* out, size_t cap) const;

private:
    static constexpr uint32_t UPDATE_WAIT_MS = 10;
    static constexpr uint32_t COPY_WAIT_MS = 50;

    StaticSemaphore_t _mutexBuf;
    SemaphoreHandle_t _mutex;
    char _buf[JSON_SNAPSHOT_CAP] = {};
    size_t _len = 0;
};
