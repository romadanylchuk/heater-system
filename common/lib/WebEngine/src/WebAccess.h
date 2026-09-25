#pragma once
#include <stddef.h>
#include <stdint.h>

// Access levels and gate outcomes shared by the pure web engine and the
// ESP32 request gate (D2/D3/D4). Public: no auth. Read: valid session
// cookie. Write: + CSRF header. Ota: + an unexpired OTA grant on that
// session.
enum class Access : uint8_t { Public, Read, Write, Ota };
enum class GateResult : uint8_t { Ok, NoSession, BadCsrf, NoOtaGrant };

constexpr const char* SESSION_COOKIE = "hsid";
constexpr const char* CSRF_HEADER = "X-CSRF-Token";
constexpr size_t SESSION_SLOTS = 4;
constexpr size_t TOKEN_BYTES = 16;                    // 128-bit
constexpr size_t TOKEN_HEX_LEN = TOKEN_BYTES * 2;      // 32
constexpr uint64_t SESSION_TTL_MS = 24ULL * 3600 * 1000;
constexpr uint32_t SESSION_TTL_S = 86400;
constexpr uint64_t OTA_GRANT_TTL_MS = 120000;
constexpr size_t WEB_FORM_BODY_MAX = 1024;             // urlencoded POST bodies
constexpr uint32_t WEB_CMD_ID_BASE = 0x80000000u;      // web command ids: base | counter (never 0)
