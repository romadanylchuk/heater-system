#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>

// A bounded, RSSI-sorted (descending) list of scanned Wi-Fi networks, built
// by the adapter from ESP32 scan results and rendered on the setup page. Pure
// data structure, no I/O.
constexpr size_t WIFI_SCAN_MAX = 16;

struct WifiScanEntry {
    char ssid[NET_NAME_TEXT_LEN + 1];
    int8_t rssi;
    bool secure;
};

class WifiScanList {
public:
    void clear();

    // Ignores a null/empty/over-length SSID. Dedupes by SSID, keeping the
    // stronger of the two RSSI readings. Keeps entries sorted by RSSI
    // (descending). When full, a new SSID replaces the weakest entry only if
    // it is stronger; otherwise it is dropped. Returns whether the list
    // changed.
    bool add(const char* ssid, int8_t rssi, bool secure);

    size_t count() const { return _count; }
    const WifiScanEntry& entry(size_t i) const { return _entries[i]; }

private:
    WifiScanEntry _entries[WIFI_SCAN_MAX] = {};
    size_t _count = 0;
};
