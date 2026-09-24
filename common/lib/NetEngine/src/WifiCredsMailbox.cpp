#include "WifiCredsMailbox.h"
#include <string.h>

namespace {

bool hasNul(const char* s, size_t len) { return len > 0 && memchr(s, '\0', len) != nullptr; }

bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool allHex(const char* s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (!isHex(s[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace

WifiCredsCheck checkWifiCreds(const char* ssid, size_t ssidLen, const char* pass, size_t passLen) {
    if (ssid == nullptr || ssidLen < 1 || ssidLen > WIFI_SSID_MAX_LEN || hasNul(ssid, ssidLen)) {
        return WifiCredsCheck::BadSsid;
    }
    if (passLen == 0) {
        return WifiCredsCheck::Ok;  // open network
    }
    if (pass == nullptr || passLen < WIFI_PASS_MIN_LEN || passLen > WIFI_PASS_MAX_LEN || hasNul(pass, passLen)) {
        return WifiCredsCheck::BadPass;
    }
    if (passLen == WIFI_PASS_MAX_LEN && !allHex(pass, passLen)) {
        return WifiCredsCheck::BadPass;  // a 64-char key is a raw PSK in hex
    }
    return WifiCredsCheck::Ok;
}

bool WifiCredsMailbox::offer(const char* ssid, size_t ssidLen, const char* pass, size_t passLen) {
    if (checkWifiCreds(ssid, ssidLen, pass, passLen) != WifiCredsCheck::Ok) {
        return false;
    }
    uint8_t expected = EMPTY;
    if (!_state.compare_exchange_strong(expected, WRITING)) {
        return false;  // a pair is pending (or being written/taken)
    }
    memcpy(_slot.ssid, ssid, ssidLen);
    _slot.ssid[ssidLen] = '\0';
    if (passLen > 0) {
        memcpy(_slot.pass, pass, passLen);
    }
    _slot.pass[passLen] = '\0';
    _state.store(FULL);
    return true;
}

bool WifiCredsMailbox::take(WifiCreds& out) {
    uint8_t expected = FULL;
    if (!_state.compare_exchange_strong(expected, READING)) {
        return false;
    }
    memcpy(&out, &_slot, sizeof(out));
    memset(_slot.pass, 0, sizeof(_slot.pass));
    _state.store(EMPTY);
    return true;
}
