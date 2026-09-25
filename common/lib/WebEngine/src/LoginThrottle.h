#pragma once
#include <stdint.h>

// Brute-force slowing shared by login and the OTA re-entry grant (D6): a
// single global counter (a single admin user makes per-IP tracking
// unnecessary). After FREE_FAILURES consecutive failures the next attempts
// are refused for LOCK_MS, and the failure count restarts. There is no
// blocking delay -- callers must poll allowed() before attempting the
// password check.
class LoginThrottle {
public:
    static constexpr uint8_t FREE_FAILURES = 5;
    static constexpr uint64_t LOCK_MS = 30000;

    // False while locked; retryS is the ceiling of the remaining lock time
    // in seconds. Unset while not locked.
    bool allowed(uint64_t nowMs, uint32_t& retryS) const;

    // The FREE_FAILURESth consecutive failure locks for LOCK_MS and resets
    // the counter (so a fresh run of FREE_FAILURES failures is needed for
    // the next lock).
    void onFailure(uint64_t nowMs);

    void onSuccess();   // resets the failure count and any active lock

private:
    uint8_t _failures = 0;
    uint64_t _lockUntilMs = 0;
};
