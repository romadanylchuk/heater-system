#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WebAccess.h"

// The pure session table behind the admin login (D3): up to SESSION_SLOTS
// RAM-only sessions, each with a session id, a CSRF synchronizer token
// (D4) and an optional OTA re-entry grant (D5). Absolute (not sliding)
// SESSION_TTL_MS expiry on a monotonic clock, so NTP jumps cannot extend or
// kill a session. `epoch` binds a session to the AdminAuth credential
// generation that created it: a credential change invalidates every
// session in one comparison, with no extra wiring (D3).
using RandomFill = void (*)(uint8_t* out, size_t len);

struct SessionTokens {
    char sid[TOKEN_HEX_LEN + 1];
    char csrf[TOKEN_HEX_LEN + 1];
};

class SessionStore {
public:
    explicit SessionStore(RandomFill rnd);

    // Evicts expired/stale-epoch slots first, then the oldest by createdMs
    // when still full. Fails (false) if `rnd` is null.
    bool create(uint64_t nowMs, uint32_t epoch, SessionTokens& out);

    // sid/csrf may be nullptr. Ok only if a slot's sid matches (constant
    // time), the epoch matches, the slot is not expired, and -- for
    // Write/Ota -- the csrf matches, and -- for Ota -- nowMs is before the
    // slot's OTA grant deadline. Any expired/stale-epoch slot encountered
    // along the way is freed.
    GateResult authorize(const char* sid, const char* csrf, Access level, uint64_t nowMs, uint32_t epoch);

    bool csrfFor(const char* sid, uint64_t nowMs, uint32_t epoch, char* out, size_t cap);
    uint32_t remainingS(const char* sid, uint64_t nowMs, uint32_t epoch) const;   // 0 if none
    bool grantOta(const char* sid, uint64_t nowMs, uint32_t epoch);              // grant = nowMs + OTA_GRANT_TTL_MS
    bool otaGranted(const char* sid, uint64_t nowMs, uint32_t epoch) const;
    void revoke(const char* sid);
    void revokeAll();
    size_t activeCount(uint64_t nowMs, uint32_t epoch) const;

private:
    struct Slot {
        bool used = false;
        char sid[TOKEN_HEX_LEN + 1] = {};
        char csrf[TOKEN_HEX_LEN + 1] = {};
        uint64_t createdMs = 0;
        uint64_t otaGrantUntilMs = 0;
        uint32_t epoch = 0;
    };

    RandomFill _rnd;
    Slot _slots[SESSION_SLOTS];

    bool expired(const Slot& s, uint64_t nowMs) const { return nowMs - s.createdMs >= SESSION_TTL_MS; }
    void freeSlot(Slot& s);
    void reapStale(uint64_t nowMs, uint32_t epoch);
    // Const-safe lookup: no mutation, used by the read-only accessors.
    const Slot* findValid(const char* sid, uint64_t nowMs, uint32_t epoch) const;
    bool fillHex(char* out) const;   // TOKEN_BYTES random bytes -> lowercase hex, out[TOKEN_HEX_LEN+1]
};
