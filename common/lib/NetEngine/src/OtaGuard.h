#pragma once
#include <stdint.h>
#include <CommonState.h>
#include <EventTypes.h>
#include "NetSignals.h"

// OTA session guard (D14): the pure state machine that decides when relay
// outputs are inhibited for an in-flight OTA, and logs the OtaUpdate
// transitions. It owns no hardware/transport; the caller (NetEsp32's facade,
// driven every fast tick) applies inhibit()/active() to
// HwRuntime::setOutputsInhibited() and requests the post-success reboot.
//
// Event value catalogue for EventType::OtaUpdate (source EVENT_SOURCE_OTA,
// aux = the OtaSource of the session): 0 started, 1 succeeded, 2 failed,
// 3 confirmed (health check passed, logged by the caller once the boot
// health monitor -- see OtaBoot.h -- resolves), 4 health-check timeout
// (logged by the caller before it forces a rollback+reboot, D15).
constexpr float OTA_EVENT_STARTED = 0;
constexpr float OTA_EVENT_SUCCEEDED = 1;
constexpr float OTA_EVENT_FAILED = 2;
constexpr float OTA_EVENT_CONFIRMED = 3;
constexpr float OTA_EVENT_HEALTH_TIMEOUT = 4;

// One drain of NetSignals' OTA fields, taken by the loop every fast tick.
// otaStarted/otaEnded are one-shot (exchanged back to 0/none); otaProgress is
// a monotonic counter compared against the caller's own last-seen value, so
// it is only ever read, never reset here.
struct OtaSignalSnapshot {
    OtaSource started = OtaSource::None;  // this drain's start signal, None if none
    bool progressed = false;              // otaProgress advanced since the last drain
    uint8_t ended = 0;                    // 0 none, 1 success, 2 failure
};

// lastProgressSeen is caller-owned state (persisted across calls), so a
// single monotonic counter can be compared without the signal itself needing
// a one-shot "consumed" flag.
OtaSignalSnapshot takeOtaSignals(NetSignals& s, uint32_t& lastProgressSeen);

struct OtaGuardResult {
    bool succeeded = false;  // this update() saw the OTA end successfully
    bool failed = false;     // this update() saw the OTA end in failure (incl. stall timeout)
    OtaSource source = OtaSource::None;  // source of the session that succeeded/failed
};

// ElegantOTA's onEnd(ok) reports !Update.hasError(), which is also true for an
// upload that never wrote a byte (empty body, or Update.write() on a closed
// session returns 0 without setting an error). Maps onEnd to the otaEnded
// code: success (1) only if ok AND bytes were written, else failure (2)
// (final-check S4).
uint8_t webOtaEndCode(bool ok, uint32_t bytesWritten);

class OtaGuard {
public:
    static constexpr uint32_t STALL_TIMEOUT_MS = 60000;

    // Processing order within one update(): start (if any) is applied first,
    // so a start+end pair drained together (espota's blocking handle() can
    // set both atomics before the loop next drains them) is handled as a
    // full session rather than dropped.
    //   - start != None  -> active=true, source=start, activity=nowMs,
    //                        log OtaUpdate(STARTED, aux=source).
    //   - progressed      -> activity=nowMs (extends the stall window).
    //   - ended == 1      -> log OtaUpdate(SUCCEEDED, aux=source), succeeded=true;
    //                        stays active (inhibit held until the caller reboots).
    //   - ended == 2      -> log OtaUpdate(FAILED, aux=source), active=false.
    //   - active && !succeeded && nowMs-activity >= STALL_TIMEOUT_MS ->
    //                        log OtaUpdate(FAILED, aux=source), active=false.
    OtaGuardResult update(const OtaSignalSnapshot& s, uint64_t nowMs, EventSink& events);

    bool active() const { return _active; }
    bool inhibit() const { return _active; }  // == active(): relay outputs must stay inhibited
    OtaSource source() const { return _source; }

private:
    bool _active = false;
    bool _succeeded = false;
    OtaSource _source = OtaSource::None;
    uint64_t _lastActivityMs = 0;
};
