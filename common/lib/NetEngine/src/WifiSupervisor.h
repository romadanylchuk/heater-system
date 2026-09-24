#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <EventTypes.h>

// Pure Wi-Fi connectivity decision machine (D3-D5): background reconnect with
// growing backoff (10, 15, 20, 25, 30, 30... s), a setup AP that runs only
// while no SSID is saved (plus the "join window" after saving new
// credentials), async-scan pass-through, and logging of outages over 30 s.
// It owns no port: every begin()/tick() call returns the actions the caller
// (NetEsp32's EspWifiPort adapter, driven by ConnectivityRuntime) must
// execute, in the order listed on WifiActions. Time is injected
// (uint64_t nowMs); no hardware, no Wi-Fi/Arduino includes.
struct WifiActions {        // caller executes in this order:
    bool disconnect = false;   // 1. station disconnect (abort attempt)
    bool stopAp = false;       // 2. AP off, STA only
    bool startAp = false;      // 3. open setup AP (AP_STA)
    bool startScan = false;    // 4. async scan
    bool connect = false;      // 5. non-blocking connect with the current ssid/pass
    bool linkWentUp = false;   // informational edges
    bool linkWentDown = false;
};

struct WifiInputs {
    const char* ssid;      // current setting ("" or nullptr = none)
    const char* pass;
    bool linkUp;            // port: associated + IP
    bool apUp;               // port: soft-AP running
    bool scanRequested;      // already consumed from NetSignals
    bool scanFinished;       // port reported done/failed this tick
};

class WifiSupervisor {
public:
    static constexpr uint32_t RETRY_FIRST_MS = 10000, RETRY_STEP_MS = 5000, RETRY_MAX_MS = 30000;
    static constexpr uint32_t OUTAGE_LOG_MS = 30000;
    static constexpr uint32_t AP_LINGER_MS = 10000, AP_JOIN_WINDOW_MS = 120000, AP_RETRY_MS = 10000;
    static constexpr uint32_t SCAN_TIMEOUT_MS = 15000;

    // Caches ssid/pass. Empty ssid -> startAp only (SetupAp). Otherwise ->
    // connect, mode Station, the outage clock starts now, first retry due at
    // RETRY_FIRST_MS.
    WifiActions begin(const char* ssid, const char* pass, uint64_t nowMs);

    WifiActions tick(const WifiInputs& in, uint64_t nowMs, EventSink& events);

    NetWifiMode mode() const { return _mode; }
    bool linkUp() const { return _linkUp; }
    bool apActive() const { return _apActive; }
    bool scanRunning() const { return _scanRunning; }
    uint32_t outageS(uint64_t nowMs) const;  // 0 while linked or when no ssid
    uint32_t connectCount() const { return _connectCount; }
    uint32_t retryMs() const { return _retry; }

private:
    static constexpr size_t SSID_CACHE_LEN = NET_NAME_TEXT_LEN;  // 32 (+1 NUL below)
    static constexpr size_t PASS_CACHE_LEN = 64;

    void cacheCreds(const char* ssid, const char* pass);

    char _ssid[SSID_CACHE_LEN + 1] = {};
    char _pass[PASS_CACHE_LEN + 1] = {};

    NetWifiMode _mode = NetWifiMode::Off;
    bool _linkUp = false;
    bool _apActive = false;
    bool _hadLink = false;  // has ever linked since begin() (D5: "since boot")

    bool _scanRunning = false;
    uint64_t _scanStartMs = 0;

    uint64_t _outageStart = 0;
    bool _outageLogged = false;

    uint32_t _retry = RETRY_FIRST_MS;
    uint64_t _nextAttempt = 0;

    uint32_t _connectCount = 0;

    uint64_t _apRetryAt = 0;
    uint64_t _apStopAt = 0;
    bool _apStopAtSet = false;
    uint64_t _joinDeadline = 0;
};
