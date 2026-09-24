#include "TimeKeeper.h"

void TimeKeeper::setFromRtc(uint32_t utc, uint64_t monoMs) {
    _valid = true;
    _source = TimeSourceKind::Rtc;
    _baseUtc = utc;
    _baseMono = monoMs;
}

void TimeKeeper::setFromNtp(uint32_t utc, uint64_t monoMs) {
    _valid = true;
    _source = TimeSourceKind::Ntp;
    _baseUtc = utc;
    _baseMono = monoMs;

    _lastNtpUtc = utc;
    _lastSyncMono = monoMs;
    _syncedSinceUp = true;
    _armed = false;
}

Timestamp TimeKeeper::now(uint64_t monoMs) const {
    if (!_valid) {
        return Timestamp{static_cast<uint32_t>(monoMs / 1000), false};
    }
    uint64_t elapsedS = (monoMs - _baseMono) / 1000;
    return Timestamp{static_cast<uint32_t>(_baseUtc + elapsedS), true};
}

void TimeKeeper::onNetworkUp(uint64_t monoMs) {
    (void)monoMs;
    _netUp = true;
    _armed = true;
    _syncedSinceUp = false;
    _hasAttempt = false;
}

void TimeKeeper::onNetworkDown() { _netUp = false; }

bool TimeKeeper::ntpSyncDue(uint64_t monoMs) const {
    if (!_netUp) {
        return false;
    }
    if (_armed && !_hasAttempt) {
        return true;
    }
    if (_lastNtpUtc != 0 && (monoMs - _lastSyncMono) >= RESYNC_INTERVAL_MS) {
        return true;
    }
    if (!_syncedSinceUp && _hasAttempt && (monoMs - _lastAttemptMono) >= RETRY_INTERVAL_MS) {
        return true;
    }
    return false;
}

void TimeKeeper::markNtpAttempt(uint64_t monoMs) {
    _lastAttemptMono = monoMs;
    _hasAttempt = true;
}
