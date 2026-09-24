#include "SensorAddress.h"
#include <string.h>

namespace {

inline char hexDigit(uint8_t nibble) {
    return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('A' + (nibble - 10));
}

inline bool hexValue(char c, uint8_t& out) {
    if (c >= '0' && c <= '9') {
        out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        out = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        out = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    return false;
}

}  // namespace

void formatAddress(const uint8_t address[8], char out[SENSOR_ADDRESS_TEXT_LEN + 1]) {
    for (size_t i = 0; i < 8; ++i) {
        out[i * 2] = hexDigit(static_cast<uint8_t>(address[i] >> 4));
        out[i * 2 + 1] = hexDigit(static_cast<uint8_t>(address[i] & 0x0F));
    }
    out[SENSOR_ADDRESS_TEXT_LEN] = '\0';
}

bool parseAddress(const char* text, uint8_t out[8]) {
    memset(out, 0, 8);
    if (text == nullptr) {
        return false;
    }
    size_t len = strlen(text);
    if (len != SENSOR_ADDRESS_TEXT_LEN) {
        return false;
    }
    for (size_t i = 0; i < 8; ++i) {
        uint8_t hi = 0;
        uint8_t lo = 0;
        if (!hexValue(text[i * 2], hi) || !hexValue(text[i * 2 + 1], lo)) {
            memset(out, 0, 8);
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool addressEquals(const uint8_t a[8], const uint8_t b[8]) {
    return memcmp(a, b, 8) == 0;
}

bool addressIsZero(const uint8_t a[8]) {
    static const uint8_t zero[8] = {};
    return memcmp(a, zero, 8) == 0;
}
