#pragma once
#include <atomic>
#include <stdint.h>
#include "AtomicIndexSet.h"

// The cross-task signal channel (stage 04): written by adapter callbacks
// running on the AsyncTCP task (MQTT inbound, ElegantOTA) or the loop task
// itself (espota, which blocks the loop task during its own handle()), and
// drained by the loop task once per fast tick. Every field is an atomic so no
// mutex/critical section is needed for these simple counters/flags/bitsets.
// Not a class: it is a plain aggregate held by ConnectivityRuntime's owner
// (NetEsp32's facade) and passed by reference to both the adapters and the
// runtime.
struct NetSignals {
    std::atomic<bool> scanRequested{false};
    AtomicIndexSet republish;                  // entity indices to re-publish (after MQTT commands)
    std::atomic<uint32_t> droppedCommands{0};   // MQTT commands dropped on a full queue (monotonic)
    std::atomic<uint8_t> otaStarted{0};         // OtaSource value of the latest start, 0 = none
    std::atomic<uint32_t> otaProgress{0};       // progress callbacks (monotonic)
    std::atomic<uint8_t> otaEnded{0};           // 0 none, 1 success, 2 failure
    // Bytes the current web OTA session wrote (ElegantOTA onProgress
    // `current`), reset on its onStart. Lets onEnd(true) with nothing written
    // be reported as a failure (final-check S4, webOtaEndCode()).
    std::atomic<uint32_t> otaWebBytes{0};
    // True while an espota session owns the global Update object (set by
    // espota's onStart, cleared by its own end/error). Read by the web
    // server's /ota/* guard on the AsyncTCP task to answer 409 instead of
    // letting ElegantOTA touch Update concurrently (review-9 Must-fix 1c).
    std::atomic<bool> espotaActive{false};
};
