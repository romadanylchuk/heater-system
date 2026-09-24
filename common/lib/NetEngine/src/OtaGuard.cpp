#include "OtaGuard.h"

OtaSignalSnapshot takeOtaSignals(NetSignals& s, uint32_t& lastProgressSeen) {
    OtaSignalSnapshot snap;
    snap.started = static_cast<OtaSource>(s.otaStarted.exchange(0));
    snap.ended = s.otaEnded.exchange(0);
    uint32_t progress = s.otaProgress.load();
    snap.progressed = progress != lastProgressSeen;
    lastProgressSeen = progress;
    return snap;
}

uint8_t webOtaEndCode(bool ok, uint32_t bytesWritten) { return (ok && bytesWritten > 0) ? 1 : 2; }

namespace {

void logOtaTransition(EventSink& events, float value, OtaSource source) {
    events.logEvent(toU16(EventType::OtaUpdate), EVENT_SOURCE_OTA, value,
        static_cast<float>(static_cast<uint8_t>(source)),
        source == OtaSource::Web ? EventReason::Web : EventReason::Logic);
}

}  // namespace

OtaGuardResult OtaGuard::update(const OtaSignalSnapshot& s, uint64_t nowMs, EventSink& events) {
    OtaGuardResult result;

    // 1. start, processed first: (re)arms the session even if this same
    // drain also carries progress/end for it.
    if (s.started != OtaSource::None) {
        _active = true;
        _succeeded = false;
        _source = s.started;
        _lastActivityMs = nowMs;
        logOtaTransition(events, OTA_EVENT_STARTED, _source);
    }

    if (!_active) {
        return result;
    }

    // 2. progress extends the stall window.
    if (s.progressed) {
        _lastActivityMs = nowMs;
    }

    // 3. end / stall.
    if (s.ended == 1) {
        _succeeded = true;
        result.succeeded = true;
        result.source = _source;
        logOtaTransition(events, OTA_EVENT_SUCCEEDED, _source);
        // Stays active: inhibit is held until the caller reboots.
    } else if (s.ended == 2) {
        result.failed = true;
        result.source = _source;
        logOtaTransition(events, OTA_EVENT_FAILED, _source);
        _active = false;
        _succeeded = false;
        _source = OtaSource::None;
    } else if (!_succeeded && nowMs - _lastActivityMs >= STALL_TIMEOUT_MS) {
        result.failed = true;
        result.source = _source;
        logOtaTransition(events, OTA_EVENT_FAILED, _source);
        _active = false;
        _succeeded = false;
        _source = OtaSource::None;
    }

    return result;
}
