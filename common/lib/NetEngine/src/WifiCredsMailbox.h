#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// Wi-Fi credential input rules (review-9 Must-fix 2 / Should-fix 4). The
// lengths match the wifiSsid/wifiPass descriptors in CommonSettings.h; the
// password is either empty (open network) or a valid WPA2 key: 8..63
// printable characters or exactly 64 hex digits. Anything else can never
// join, and with a non-empty SSID the supervisor does not fall back to the
// setup AP, so a bad value would lock a Station-mode device out.
constexpr size_t WIFI_SSID_MAX_LEN = 32;
constexpr size_t WIFI_PASS_MIN_LEN = 8;
constexpr size_t WIFI_PASS_MAX_LEN = 64;

enum class WifiCredsCheck : uint8_t {
    Ok,
    BadSsid,  // empty, longer than 32 or containing a NUL
    BadPass,  // 1..7, longer than 64, 64 non-hex, or containing a NUL
};

// ssidLen/passLen are the received lengths (e.g. String::length()), so an
// embedded NUL -- which would silently truncate the stored value -- is
// detected and rejected. A null pointer is treated as "" (only valid with
// length 0).
WifiCredsCheck checkWifiCreds(const char* ssid, size_t ssidLen, const char* pass, size_t passLen);

struct WifiCreds {
    char ssid[WIFI_SSID_MAX_LEN + 1] = {};
    char pass[WIFI_PASS_MAX_LEN + 1] = {};
};

// Single-slot mailbox that hands a complete SSID+password pair from the web
// handler (AsyncTCP task) to the loop task, which applies BOTH settings in the
// same call (review-9 Must-fix 2: two separate queue posts could apply only
// one half and leave the device retrying the old SSID with the new password).
//
// Lock-free: one atomic state walks Empty -> Writing -> Full -> Reading ->
// Empty; only the CAS winner touches the slot, so a writer and the reader can
// never see a half-written pair and a second writer cannot overwrite a pair
// the loop has not taken yet.
class WifiCredsMailbox {
public:
    // Any task. Validates (checkWifiCreds) and stores the pair. False when the
    // input is invalid or a previous pair is still pending (caller answers
    // 400 / 503 respectively -- use checkWifiCreds first to tell them apart).
    bool offer(const char* ssid, size_t ssidLen, const char* pass, size_t passLen);

    // Loop task. Moves the pending pair into out and empties the slot (the
    // slot's password copy is wiped). False when nothing is pending.
    bool take(WifiCreds& out);

    bool pending() const { return _state.load() == FULL; }

private:
    static constexpr uint8_t EMPTY = 0;
    static constexpr uint8_t WRITING = 1;
    static constexpr uint8_t FULL = 2;
    static constexpr uint8_t READING = 3;

    std::atomic<uint8_t> _state{EMPTY};
    WifiCreds _slot;
};
