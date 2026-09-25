#pragma once
#include <stddef.h>
#include <stdint.h>
#include <ConfigSchema.h>
#include <SettingDescriptor.h>

// Pure request-input parsers for the stage-05 write routes (settings form,
// sensor assign/clear, /api/cmd?id=). Handlers run on the AsyncTCP task, so
// everything here reads only the immutable constexpr schema tables and the
// caller's buffers; no ConfigEngine access, no heap.
enum class InputError : uint8_t {
    Ok,
    UnknownKey,
    WifiKey,
    BadNumber,
    BadBool,
    TooLong,
    EmbeddedNul,
    BadIndex,
    BadAddress,
    Missing,
};

struct ParsedSetting {
    uint16_t index;      // global index across schema tables (same order as ConfigEngine)
    SettingType type;
    float number;        // Int/Float value; Bool as 0/1
    const char* text;    // Text only: points at the caller's value (not copied)
    size_t textLen;
};

// wifiSsid/wifiPass -> WifiKey (they go through the Wi-Fi mailbox instead).
// Int: optional sign + digits, at most 16 characters in all; Float: strtof full-consume, finite; Bool:
// 0/1/true/false; Text: len <= maxLen, no NUL (valueLen from String::length()).
// Range clamping is left to ConfigEngine. A null key/value gives Missing.
InputError parseSettingInput(const ConfigSchema& s, const char* key, const char* value, size_t valueLen,
    ParsedSetting& out);

// "28ff0a1b2c3d4e5f" or with ':'/'-' separators; exactly 16 hex digits -> 8
// bytes in text order. Any failure zeroes out and gives BadAddress (Missing
// for null/empty text).
InputError parseRomAddress(const char* text, size_t len, uint8_t out[8]);

// Decimal digits only, value < sensorCount -> Ok, else BadIndex (Missing for
// null/empty text).
InputError parseLogicalIndex(const char* text, size_t sensorCount, uint8_t& out);

// Decimal digits only (at most 16 characters, checked before scanning),
// fits uint32_t. Used for /api/cmd?id=.
bool parseU32(const char* text, uint32_t& out);

// "ok", "unknown_key", "wifi_key", "bad_number", "bad_bool", "too_long",
// "embedded_nul", "bad_index", "bad_address", "missing".
const char* inputErrorKey(InputError e);
