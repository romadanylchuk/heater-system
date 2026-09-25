#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>

class AsyncWebServerRequest;

// Mutex-guarded JSON document over caller-owned static storage (stage 05,
// D18 pattern of NetEsp32's JsonSnapshot): the loop task rebuilds it from
// CommonState/ConfigEngine every slow tick, and GET handlers on the AsyncTCP
// task stream a copy out, so no handler ever reads CommonState. update()
// gives up after 10 ms (the document stays one tick stale); send() waits at
// most 50 ms and answers 503 {"ok":false,"error":"busy"} on timeout or while
// the document is still empty.
class SharedJsonBuffer {
public:
    // Static mutex: no heap, safe at static-init time. buf/cap must outlive
    // this object.
    SharedJsonBuffer(char* buf, size_t cap);

    // Loop task. len == 0 (a builder overflow) or len >= cap is rejected and
    // the previous document kept.
    // false when rejected (len 0/oversized) or the mutex wait timed out.
    bool update(const char* json, size_t len);

    // AsyncTCP task: copies under the mutex into an AsyncResponseStream and
    // sends 200 application/json with Cache-Control: no-store.
    void send(AsyncWebServerRequest* r) const;

private:
    static constexpr uint32_t UPDATE_WAIT_MS = 10;
    static constexpr uint32_t SEND_WAIT_MS = 50;

    StaticSemaphore_t _mutexBuf;
    SemaphoreHandle_t _mutex;
    char* _buf;
    size_t _cap;
    size_t _len = 0;
};
