#pragma once
#include <stdint.h>

#include "Clock.h"
#include "CommonState.h"

// Pure time validity/source/NTP-scheduling state machine (D17). No hardware,
// RTC or network access: the ESP32 adapter (TimeService, CoreEsp32) drives it
// with RTC reads, SNTP sync results and Wi-Fi up/down notifications.
class TimeKeeper {
public:
    static constexpr uint64_t RESYNC_INTERVAL_MS = 24ULL * 3600 * 1000;
    static constexpr uint64_t RETRY_INTERVAL_MS = 5ULL * 60 * 1000;

    // Establishes a valid time base from an RTC read (source = Rtc).
    void setFromRtc(uint32_t utc, uint64_t monoMs);

    // Establishes a valid time base from a completed NTP sync (source = Ntp);
    // also records the sync for lastNtpSyncUtc()/ntpSyncDue() bookkeeping.
    void setFromNtp(uint32_t utc, uint64_t monoMs);

    bool isValid() const { return _valid; }
    TimeSourceKind source() const { return _source; }

    // Valid: base UTC advanced by (monoMs - baseMono) / 1000, realTime = true.
    // Invalid: uptime seconds (monoMs / 1000), realTime = false.
    Timestamp now(uint64_t monoMs) const;

    // 0 if there has never been a successful NTP sync.
    uint32_t lastNtpSyncUtc() const { return _lastNtpUtc; }

    // Arms an immediate sync attempt (network just came up / reconnected).
    void onNetworkUp(uint64_t monoMs);
    void onNetworkDown();

    // network up && (armed-and-not-yet-attempted || >=24h since last sync ||
    // (not synced since this network-up && >=5 min since last attempt)).
    bool ntpSyncDue(uint64_t monoMs) const;

    // Records that a sync was attempted (SNTP (re)started), so the retry timer
    // in ntpSyncDue() counts from here.
    void markNtpAttempt(uint64_t monoMs);

private:
    bool _valid = false;
    TimeSourceKind _source = TimeSourceKind::None;
    uint32_t _baseUtc = 0;
    uint64_t _baseMono = 0;

    bool _netUp = false;
    bool _armed = false;
    bool _syncedSinceUp = false;
    uint64_t _lastSyncMono = 0;
    uint64_t _lastAttemptMono = 0;
    bool _hasAttempt = false;
    uint32_t _lastNtpUtc = 0;
};
