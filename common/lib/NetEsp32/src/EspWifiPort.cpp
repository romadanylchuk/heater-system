#include "EspWifiPort.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <string.h>

namespace {

void copyIp(const IPAddress& ip, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    snprintf(out, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

void EspWifiPort::init(const char* hostname) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    // setHostname() must precede the mode change to be applied to the STA netif.
    WiFi.setHostname(hostname);
    WiFi.mode(WIFI_STA);
}

void EspWifiPort::connect(const char* ssid, const char* pass) {
    WiFi.begin(ssid, (pass != nullptr && pass[0] != '\0') ? pass : nullptr);
}

void EspWifiPort::disconnect() { WiFi.disconnect(false, false); }

bool EspWifiPort::startAp(const char* apSsid) {
    if (!WiFi.mode(WIFI_AP_STA)) {
        return false;
    }
    return WiFi.softAP(apSsid);  // open AP, default 192.168.4.1
}

void EspWifiPort::stopAp() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

bool EspWifiPort::apUp() {
    const wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_AP && mode != WIFI_AP_STA) {
        return false;
    }
    return WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
}

bool EspWifiPort::linkUp() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

int8_t EspWifiPort::rssi() {
    const int32_t v = WiFi.RSSI();
    if (v < INT8_MIN) {
        return INT8_MIN;
    }
    if (v > INT8_MAX) {
        return INT8_MAX;
    }
    return static_cast<int8_t>(v);
}

void EspWifiPort::localIp(char* out, size_t cap) { copyIp(WiFi.localIP(), out, cap); }

void EspWifiPort::apIp(char* out, size_t cap) { copyIp(WiFi.softAPIP(), out, cap); }

bool EspWifiPort::startScan() {
    // async = true, show_hidden = false: returns WIFI_SCAN_RUNNING immediately.
    return WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING;
}

int EspWifiPort::scanStatus() {
    const int16_t s = WiFi.scanComplete();
    if (s == WIFI_SCAN_RUNNING) {
        return WIFI_SCAN_RUNNING_STATUS;
    }
    if (s < 0) {
        return WIFI_SCAN_FAILED_STATUS;
    }
    return s;
}

bool EspWifiPort::scanEntry(int i, WifiScanEntry& out) {
    if (i < 0 || i > 255) {
        return false;
    }
    const uint8_t item = static_cast<uint8_t>(i);
    // WiFi.SSID(i) returns an Arduino String; copied (and truncated to the
    // entry size) right here so no String escapes the adapter.
    const String ssid = WiFi.SSID(item);
    if (ssid.length() == 0 || ssid.length() > NET_NAME_TEXT_LEN) {
        return false;
    }
    memcpy(out.ssid, ssid.c_str(), ssid.length());
    out.ssid[ssid.length()] = '\0';
    const int32_t r = WiFi.RSSI(item);
    out.rssi = static_cast<int8_t>(r < INT8_MIN ? INT8_MIN : (r > INT8_MAX ? INT8_MAX : r));
    out.secure = WiFi.encryptionType(item) != WIFI_AUTH_OPEN;
    return true;
}

void EspWifiPort::scanClear() { WiFi.scanDelete(); }

void EspWifiPort::startMdns(const char* hostname) {
    // Idempotent: the IDF mdns service follows the netif, so later link-ups
    // need no re-registration.
    if (_mdnsStarted) {
        return;
    }
    if (MDNS.begin(hostname)) {
        MDNS.addService("http", "tcp", 80);
        _mdnsStarted = true;
    }
}
