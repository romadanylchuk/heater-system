#include "WebInput.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

// Longest accepted numeric text (final-check Should 3): int32/uint32 need at
// most 11 characters; 16 leaves room for a few leading zeros and bounds the
// scan whatever length the caller passes. Longer input is refused unscanned.
constexpr size_t NUMERIC_TEXT_MAX_LEN = 16;

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hasEmbeddedNul(const char* value, size_t len) { return memchr(value, '\0', len) != nullptr; }

// Optional sign + at least one digit, the whole of [value, value+len), in int32 range.
bool parseIntText(const char* value, size_t len, float& out) {
    if (len == 0 || len > NUMERIC_TEXT_MAX_LEN || hasEmbeddedNul(value, len)) return false;
    size_t i = 0;
    bool negative = false;
    if (value[0] == '+' || value[0] == '-') {
        negative = value[0] == '-';
        i = 1;
    }
    if (i >= len) return false;
    int64_t acc = 0;
    for (; i < len; ++i) {
        if (!isDigit(value[i])) return false;
        acc = acc * 10 + (value[i] - '0');
        if (acc > 2147483648LL) return false;
    }
    if (negative) acc = -acc;
    if (acc > 2147483647LL || acc < -2147483648LL) return false;
    out = static_cast<float>(acc);
    return true;
}

// strtof over the whole value (no leading whitespace), finite result.
bool parseFloatText(const char* value, size_t len, float& out) {
    if (len == 0 || len > 48 || hasEmbeddedNul(value, len)) return false;
    char first = value[0];
    if (!(isDigit(first) || first == '+' || first == '-' || first == '.')) return false;
    char buf[49];
    memcpy(buf, value, len);
    buf[len] = '\0';
    char* end = nullptr;
    float v = strtof(buf, &end);
    if (end != buf + len) return false;
    if (!isfinite(v)) return false;
    out = v;
    return true;
}

bool textEquals(const char* value, size_t len, const char* lit) {
    size_t n = strlen(lit);
    return n == len && memcmp(value, lit, n) == 0;
}

}  // namespace

InputError parseSettingInput(const ConfigSchema& s, const char* key, const char* value, size_t valueLen,
        ParsedSetting& out) {
    out = ParsedSetting{0, SettingType::Int, 0.0f, nullptr, 0};
    if (key == nullptr || value == nullptr) return InputError::Missing;

    const SettingDescriptor* desc = nullptr;
    size_t global = 0;
    for (size_t t = 0; t < s.tableCount && desc == nullptr; ++t) {
        const SettingsTable& table = s.tables[t];
        for (size_t i = 0; i < table.count; ++i, ++global) {
            if (strcmp(table.items[i].key, key) == 0) {
                desc = &table.items[i];
                break;
            }
        }
    }
    if (desc == nullptr) return InputError::UnknownKey;
    if (strcmp(key, "wifiSsid") == 0 || strcmp(key, "wifiPass") == 0) return InputError::WifiKey;

    out.index = static_cast<uint16_t>(global);
    out.type = desc->type;
    switch (desc->type) {
        case SettingType::Int:
            if (!parseIntText(value, valueLen, out.number)) return InputError::BadNumber;
            return InputError::Ok;
        case SettingType::Float:
            if (!parseFloatText(value, valueLen, out.number)) return InputError::BadNumber;
            return InputError::Ok;
        case SettingType::Bool:
            if (textEquals(value, valueLen, "1") || textEquals(value, valueLen, "true")) {
                out.number = 1.0f;
                return InputError::Ok;
            }
            if (textEquals(value, valueLen, "0") || textEquals(value, valueLen, "false")) {
                out.number = 0.0f;
                return InputError::Ok;
            }
            return InputError::BadBool;
        case SettingType::Text:
            if (hasEmbeddedNul(value, valueLen)) return InputError::EmbeddedNul;
            if (valueLen > desc->maxLen) return InputError::TooLong;
            out.text = value;
            out.textLen = valueLen;
            return InputError::Ok;
    }
    return InputError::UnknownKey;
}

InputError parseRomAddress(const char* text, size_t len, uint8_t out[8]) {
    memset(out, 0, 8);
    if (text == nullptr || len == 0) return InputError::Missing;
    uint8_t bytes[8] = {};
    size_t digits = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        if (c == ':' || c == '-') {
            // separators only between byte pairs
            if (digits == 0 || digits % 2 != 0 || digits >= 16) return InputError::BadAddress;
            continue;
        }
        int v = hexValue(c);
        if (v < 0 || digits >= 16) return InputError::BadAddress;
        bytes[digits / 2] = static_cast<uint8_t>((bytes[digits / 2] << 4) | v);
        ++digits;
    }
    if (digits != 16) return InputError::BadAddress;
    memcpy(out, bytes, 8);
    return InputError::Ok;
}

InputError parseLogicalIndex(const char* text, size_t sensorCount, uint8_t& out) {
    out = 0;
    if (text == nullptr || text[0] == '\0') return InputError::Missing;
    uint32_t v = 0;
    if (!parseU32(text, v)) return InputError::BadIndex;
    if (v >= sensorCount || v > 0xFE) return InputError::BadIndex;
    out = static_cast<uint8_t>(v);
    return InputError::Ok;
}

bool parseU32(const char* text, uint32_t& out) {
    out = 0;
    if (text == nullptr || text[0] == '\0') return false;
    // Bounded length check before scanning digits (never walks past the cap).
    size_t len = 0;
    while (len <= NUMERIC_TEXT_MAX_LEN && text[len] != '\0') ++len;
    if (len > NUMERIC_TEXT_MAX_LEN) return false;
    uint64_t acc = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (!isDigit(*p)) return false;
        acc = acc * 10 + static_cast<uint64_t>(*p - '0');
        if (acc > 0xFFFFFFFFull) return false;
    }
    out = static_cast<uint32_t>(acc);
    return true;
}

const char* inputErrorKey(InputError e) {
    switch (e) {
        case InputError::Ok:          return "ok";
        case InputError::UnknownKey:  return "unknown_key";
        case InputError::WifiKey:     return "wifi_key";
        case InputError::BadNumber:   return "bad_number";
        case InputError::BadBool:     return "bad_bool";
        case InputError::TooLong:     return "too_long";
        case InputError::EmbeddedNul: return "embedded_nul";
        case InputError::BadIndex:    return "bad_index";
        case InputError::BadAddress:  return "bad_address";
        case InputError::Missing:     return "missing";
    }
    return "unknown";
}
