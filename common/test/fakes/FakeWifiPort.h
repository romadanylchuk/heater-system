#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "../../lib/NetEngine/src/WifiPort.h"

// Header-only WifiPort fake for native tests: scriptable link/AP/scan state
// plus recorded calls (init hostname, connect ssid/pass, startAp ssid, mdns
// call count/hostname). Not shipped in firmware (test-only,
// common/test/fakes/).
class FakeWifiPort : public WifiPort {
public:
    bool link = false;
    bool ap = false;
    bool apStartResult = true;
    int scanStatusValue = WIFI_SCAN_FAILED_STATUS;  // default: nothing running/ready
    std::vector<WifiScanEntry> scanEntries;
    int8_t rssiValue = -50;
    std::string localIpValue = "0.0.0.0";
    std::string apIpValue = "192.168.4.1";

    std::string initHostname;
    int initCount = 0;
    std::string connectSsid;
    std::string connectPass;
    int connectCount = 0;
    int disconnectCount = 0;
    std::string startApSsid;
    int startApCount = 0;
    int stopApCount = 0;
    int startScanCount = 0;
    int scanClearCount = 0;
    std::string mdnsHostname;
    int mdnsCount = 0;

    void init(const char* hostname) override {
        initHostname = hostname != nullptr ? hostname : "";
        ++initCount;
    }

    void connect(const char* ssid, const char* pass) override {
        connectSsid = ssid != nullptr ? ssid : "";
        connectPass = pass != nullptr ? pass : "";
        ++connectCount;
    }

    void disconnect() override { ++disconnectCount; }

    bool startAp(const char* apSsid) override {
        startApSsid = apSsid != nullptr ? apSsid : "";
        ++startApCount;
        if (apStartResult) {
            ap = true;
        }
        return apStartResult;
    }

    void stopAp() override {
        ++stopApCount;
        ap = false;
    }

    bool apUp() override { return ap; }
    bool linkUp() override { return link; }
    int8_t rssi() override { return rssiValue; }

    void localIp(char* out, size_t cap) override { copyInto(localIpValue, out, cap); }
    void apIp(char* out, size_t cap) override { copyInto(apIpValue, out, cap); }

    bool startScan() override {
        ++startScanCount;
        return true;
    }

    int scanStatus() override { return scanStatusValue; }

    bool scanEntry(int i, WifiScanEntry& out) override {
        if (i < 0 || static_cast<size_t>(i) >= scanEntries.size()) {
            return false;
        }
        out = scanEntries[static_cast<size_t>(i)];
        return true;
    }

    void scanClear() override { ++scanClearCount; }

    void startMdns(const char* hostname) override {
        mdnsHostname = hostname != nullptr ? hostname : "";
        ++mdnsCount;
    }

private:
    static void copyInto(const std::string& v, char* out, size_t cap) {
        if (cap == 0) {
            return;
        }
        size_t n = v.size();
        if (n >= cap) {
            n = cap - 1;
        }
        memcpy(out, v.data(), n);
        out[n] = '\0';
    }
};
