#include "LoginThrottle.h"

bool LoginThrottle::allowed(uint64_t nowMs, uint32_t& retryS) const {
    if (nowMs < _lockUntilMs) {
        uint64_t remainMs = _lockUntilMs - nowMs;
        retryS = static_cast<uint32_t>((remainMs + 999) / 1000);
        return false;
    }
    return true;
}

void LoginThrottle::onFailure(uint64_t nowMs) {
    ++_failures;
    if (_failures >= FREE_FAILURES) {
        _lockUntilMs = nowMs + LOCK_MS;
        _failures = 0;
    }
}

void LoginThrottle::onSuccess() {
    _failures = 0;
    _lockUntilMs = 0;
}
