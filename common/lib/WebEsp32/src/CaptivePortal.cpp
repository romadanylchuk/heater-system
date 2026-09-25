#include "CaptivePortal.h"
#include <Arduino.h>
#include <string.h>

void CaptivePortal::update(bool apActive, const char* apIp) {
    if (apActive && !_started) {
        IPAddress ip;
        if (apIp == nullptr || !ip.fromString(apIp)) {
            return;  // no usable AP address yet: retry on the next tick
        }
        size_t len = strnlen(apIp, NET_IP_TEXT_LEN);
        memcpy(_apIp, apIp, len);
        _apIp[len] = '\0';
        if (!_dns.start(DNS_PORT, "*", ip)) {
            Serial.println("[web] captive DNS start failed");
            return;
        }
        _started = true;
        _active.store(true, std::memory_order_release);  // after _apIp is complete
        Serial.printf("[web] captive DNS on %s\n", _apIp);
    } else if (!apActive && _started) {
        _active.store(false, std::memory_order_release);
        _dns.stop();
        _started = false;
        Serial.println("[web] captive DNS stopped");
    }
}

void CaptivePortal::process() {
    if (_started) {
        _dns.processNextRequest();
    }
}
