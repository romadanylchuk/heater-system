#pragma once
#include <WifiPort.h>

// WifiPort over the Arduino-ESP32 WiFi + ESPmDNS libraries (D3/D4). Every call
// is non-blocking (no sleeping, no waiting for the connect result). Only the pure
// WifiSupervisor (driven by ConnectivityRuntime on the loop task) decides when
// to connect, start/stop the setup AP or scan; persistence and Arduino
// auto-reconnect are switched off in init() so nothing else drives the radio.
class EspWifiPort : public WifiPort {
public:
    void init(const char* hostname) override;
    void connect(const char* ssid, const char* pass) override;
    void disconnect() override;
    bool startAp(const char* apSsid) override;
    void stopAp() override;
    bool apUp() override;
    bool linkUp() override;
    int8_t rssi() override;
    void localIp(char* out, size_t cap) override;
    void apIp(char* out, size_t cap) override;
    bool startScan() override;
    int scanStatus() override;
    bool scanEntry(int i, WifiScanEntry& out) override;
    void scanClear() override;
    void startMdns(const char* hostname) override;

private:
    bool _mdnsStarted = false;
};
