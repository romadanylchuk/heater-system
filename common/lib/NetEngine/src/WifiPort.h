#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WifiScanList.h"

// Port interface (D1/D2) the pure connectivity layer drives; NetEsp32's
// EspWifiPort (a later phase) implements it over WiFi.h/esp_wifi. Only
// WifiScanList (itself pure) is pulled in -- no WiFi/Arduino includes here
// (D22). common/test/fakes/FakeWifiPort implements it for native tests.
constexpr int WIFI_SCAN_RUNNING_STATUS = -1;
constexpr int WIFI_SCAN_FAILED_STATUS = -2;

class WifiPort {
public:
    virtual ~WifiPort() = default;

    // persistent(false), autoReconnect(false), sets the hostname, STA mode.
    virtual void init(const char* hostname) = 0;

    // Non-blocking connect attempt with the given credentials.
    virtual void connect(const char* ssid, const char* pass) = 0;

    virtual void disconnect() = 0;

    // Open setup AP (AP_STA mode, 192.168.4.1). Returns false on failure.
    virtual bool startAp(const char* apSsid) = 0;

    virtual void stopAp() = 0;
    virtual bool apUp() = 0;

    // Associated + IP assigned.
    virtual bool linkUp() = 0;

    virtual int8_t rssi() = 0;
    virtual void localIp(char* out, size_t cap) = 0;
    virtual void apIp(char* out, size_t cap) = 0;

    virtual bool startScan() = 0;

    // WIFI_SCAN_RUNNING_STATUS while in progress, WIFI_SCAN_FAILED_STATUS on
    // failure, otherwise >= 0 is the number of results ready.
    virtual int scanStatus() = 0;

    virtual bool scanEntry(int i, WifiScanEntry& out) = 0;
    virtual void scanClear() = 0;

    // Idempotent: safe to call again once already started.
    virtual void startMdns(const char* hostname) = 0;
};
