#include "WifiScanList.h"
#include <string.h>

namespace {
void copySsid(char* dst, const char* src) {
    strncpy(dst, src, NET_NAME_TEXT_LEN);
    dst[NET_NAME_TEXT_LEN] = '\0';
}
}  // namespace

void WifiScanList::clear() { _count = 0; }

bool WifiScanList::add(const char* ssid, int8_t rssi, bool secure) {
    if (!ssid || ssid[0] == '\0' || strlen(ssid) > NET_NAME_TEXT_LEN) {
        return false;
    }

    // Dedupe: an existing entry for this SSID is only updated (and
    // re-sorted) if the new reading is stronger; otherwise nothing changes.
    for (size_t i = 0; i < _count; ++i) {
        if (strcmp(_entries[i].ssid, ssid) != 0) {
            continue;
        }
        if (rssi <= _entries[i].rssi) {
            return false;
        }
        WifiScanEntry updated = _entries[i];
        updated.rssi = rssi;
        updated.secure = secure;
        for (size_t j = i; j + 1 < _count; ++j) {
            _entries[j] = _entries[j + 1];
        }
        --_count;
        size_t pos = _count;
        while (pos > 0 && _entries[pos - 1].rssi < updated.rssi) {
            _entries[pos] = _entries[pos - 1];
            --pos;
        }
        _entries[pos] = updated;
        ++_count;
        return true;
    }

    WifiScanEntry fresh{};
    copySsid(fresh.ssid, ssid);
    fresh.rssi = rssi;
    fresh.secure = secure;

    if (_count < WIFI_SCAN_MAX) {
        size_t pos = _count;
        while (pos > 0 && _entries[pos - 1].rssi < rssi) {
            _entries[pos] = _entries[pos - 1];
            --pos;
        }
        _entries[pos] = fresh;
        ++_count;
        return true;
    }

    // Full: replace the weakest entry only if the new one is stronger.
    size_t weakest = 0;
    for (size_t i = 1; i < _count; ++i) {
        if (_entries[i].rssi < _entries[weakest].rssi) {
            weakest = i;
        }
    }
    if (rssi <= _entries[weakest].rssi) {
        return false;
    }
    for (size_t j = weakest; j + 1 < _count; ++j) {
        _entries[j] = _entries[j + 1];
    }
    --_count;
    size_t pos = _count;
    while (pos > 0 && _entries[pos - 1].rssi < rssi) {
        _entries[pos] = _entries[pos - 1];
        --pos;
    }
    _entries[pos] = fresh;
    ++_count;
    return true;
}
