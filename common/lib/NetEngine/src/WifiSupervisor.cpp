#include "WifiSupervisor.h"
#include <algorithm>
#include <string.h>

namespace {
void copyTrunc(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}
}  // namespace

void WifiSupervisor::cacheCreds(const char* ssid, const char* pass) {
    copyTrunc(_ssid, sizeof(_ssid), ssid);
    copyTrunc(_pass, sizeof(_pass), pass);
}

WifiActions WifiSupervisor::begin(const char* ssid, const char* pass, uint64_t nowMs) {
    cacheCreds(ssid, pass);

    WifiActions a{};
    _linkUp = false;
    _hadLink = false;
    _scanRunning = false;
    _connectCount = 0;
    _outageLogged = false;
    _apStopAtSet = false;

    if (_ssid[0] == '\0') {
        _mode = NetWifiMode::SetupAp;
        _apActive = true;
        a.startAp = true;
        _apRetryAt = nowMs + AP_RETRY_MS;
    } else {
        _mode = NetWifiMode::Station;
        _apActive = false;
        _outageStart = nowMs;
        _retry = RETRY_FIRST_MS;
        _nextAttempt = nowMs + _retry;
        a.connect = true;
    }
    return a;
}

uint32_t WifiSupervisor::outageS(uint64_t nowMs) const {
    if (_ssid[0] == '\0' || _linkUp || nowMs <= _outageStart) {
        return 0;
    }
    return static_cast<uint32_t>((nowMs - _outageStart) / 1000);
}

WifiActions WifiSupervisor::tick(const WifiInputs& in, uint64_t nowMs, EventSink& events) {
    WifiActions a{};
    const char* inSsid = in.ssid ? in.ssid : "";
    const char* inPass = in.pass ? in.pass : "";

    // 1. Credential change vs the cache.
    if (strcmp(inSsid, _ssid) != 0 || strcmp(inPass, _pass) != 0) {
        const bool wasLinkUp = _linkUp;
        const bool wasApActive = _apActive;
        cacheCreds(inSsid, inPass);

        if (_ssid[0] == '\0') {
            a.disconnect = true;
            if (!wasApActive) {
                a.startAp = true;
                _apRetryAt = nowMs + AP_RETRY_MS;
            }
            _mode = NetWifiMode::SetupAp;
            _apActive = true;
        } else {
            a.disconnect = true;
            a.connect = true;
            _retry = RETRY_FIRST_MS;
            _nextAttempt = nowMs + RETRY_FIRST_MS;
            if (wasApActive) {
                _mode = NetWifiMode::SetupApJoining;
                _joinDeadline = nowMs + AP_JOIN_WINDOW_MS;
                _apStopAtSet = false;
            } else {
                _mode = NetWifiMode::Station;
            }
            if (wasLinkUp) {
                // Anticipated edge only (D5): no immediate outage log. The
                // normal 30 s outage-log rule (step 3) still applies to the
                // fresh attempt below if it also fails to relink quickly.
                a.linkWentDown = true;
            }
            _outageStart = nowMs;
            _outageLogged = false;
        }
    }

    // 2. Link edges (ssid non-empty).
    if (_ssid[0] != '\0') {
        if (in.linkUp && !_linkUp) {
            ++_connectCount;
            a.linkWentUp = true;
            if (_outageLogged) {
                const uint32_t outage = static_cast<uint32_t>((nowMs - _outageStart) / 1000);
                events.logEvent(toU16(EventType::WifiConnected), EVENT_SOURCE_WIFI, static_cast<float>(outage), 0.0f,
                    EventReason::Logic);
            }
            _outageLogged = false;
            _hadLink = true;
            _retry = RETRY_FIRST_MS;
            if (_mode == NetWifiMode::SetupApJoining) {
                _apStopAt = nowMs + AP_LINGER_MS;
                _apStopAtSet = true;
            }
        } else if (!in.linkUp && _linkUp) {
            a.linkWentDown = true;
            _outageStart = nowMs;
            _outageLogged = false;
            a.disconnect = true;
            a.connect = true;
            _retry = RETRY_FIRST_MS;
            _nextAttempt = nowMs + RETRY_FIRST_MS;
        }
    }
    _linkUp = in.linkUp;

    // 3. Outage log: once, when a real (ssid non-empty) outage reaches 30 s.
    if (_ssid[0] != '\0' && !_linkUp && !_outageLogged && (nowMs - _outageStart) >= OUTAGE_LOG_MS) {
        const float value = _hadLink ? 0.0f : 1.0f;
        events.logEvent(toU16(EventType::WifiDisconnected), EVENT_SOURCE_WIFI, value, 0.0f, EventReason::Logic);
        _outageLogged = true;
    }

    // 4. Scan.
    if (in.scanRequested && !_scanRunning) {
        if (_ssid[0] != '\0' && !_linkUp) {
            a.disconnect = true;
        }
        a.startScan = true;
        _scanRunning = true;
        _scanStartMs = nowMs;
    } else if (_scanRunning) {
        const bool timedOut = (nowMs - _scanStartMs) >= SCAN_TIMEOUT_MS;
        if (in.scanFinished || timedOut) {
            _scanRunning = false;
            if (_ssid[0] != '\0' && !_linkUp) {
                a.connect = true;
                _retry = RETRY_FIRST_MS;
                _nextAttempt = nowMs + RETRY_FIRST_MS;
            }
        }
    }

    // 5. Retry: growing backoff while disconnected and not scanning.
    if (_ssid[0] != '\0' && !_linkUp && !_scanRunning && nowMs >= _nextAttempt) {
        a.disconnect = true;
        a.connect = true;
        _retry = std::min(_retry + RETRY_STEP_MS, RETRY_MAX_MS);
        _nextAttempt = nowMs + _retry;
    }

    // 6. AP maintenance.
    if (_mode == NetWifiMode::SetupAp || _mode == NetWifiMode::SetupApJoining) {
        if (!in.apUp && nowMs >= _apRetryAt) {
            a.startAp = true;
            _apRetryAt = nowMs + AP_RETRY_MS;
        }
    }
    if (_mode == NetWifiMode::SetupApJoining) {
        const bool lingerExpired = _apStopAtSet && nowMs >= _apStopAt;
        const bool joinExpired = nowMs >= _joinDeadline;
        if (lingerExpired || joinExpired) {
            a.stopAp = true;
            _apActive = false;
            _apStopAtSet = false;
            _mode = NetWifiMode::Station;
        }
    }

    return a;
}
