#pragma once
#include <freertos/FreeRTOS.h>
#include <AdminCredentials.h>
#include <stddef.h>
#include <stdint.h>

// Thread-safe admin credential store (D17). The ONLY credential source:
// stage 05's SessionGate/login (verify), the OTA re-entry grant
// (verifyPassword) and espota (via the same webPass). HTTP digest auth is
// gone (stage 05, D2): every protected web route goes through a
// RequestGate session check instead.
//
// set() runs on the loop task (at begin and whenever webUser/webPass change);
// verify*() run on the AsyncTCP task. The credentials live in a pure
// AdminCredentials (WebEngine) guarded by a portMUX spinlock: set() assigns
// it AND bumps its epoch inside one critical section; verify*() copy the
// whole pair (with its epoch) under the lock, compare the copy in constant
// time outside it and wipe it, so a concurrent set() can never tear a buffer
// the web task is reading.
//
// Epoch (D3, review-3 Should-fix 2): verify*() report, through epochOut, the
// epoch of the exact credential generation they checked. SessionGate binds
// the new session to that value (never to a separate epoch() read), so a
// login that raced a credential change and verified the OLD password gets
// the OLD epoch and its session is invalid from the first request after the
// change. Any credential change (web form, backup import, factory reset)
// therefore invalidates every existing session.
class AdminAuth {
public:
    static constexpr size_t USER_MAX = AdminCredentials::USER_MAX;
    static constexpr size_t PASS_MAX = AdminCredentials::PASS_MAX;

    // Copies (truncated to USER_MAX/PASS_MAX); null copies as "". Bumps
    // epoch() under the same lock.
    void set(const char* user, const char* pass);

    // Constant-time compare against a locked copy; false when the stored
    // user or password is empty (fail closed) or on mismatch. On success
    // epochOut (if non-null) receives the epoch of the checked pair.
    bool verify(const char* user, size_t userLen, const char* pass, size_t passLen,
        uint32_t* epochOut = nullptr) const;

    // Password-only check for the OTA re-entry grant (D5). Same rules.
    bool verifyPassword(const char* pass, size_t passLen, uint32_t* epochOut = nullptr) const;

    // The current credential generation (read under the lock).
    uint32_t epoch() const;

private:
    void copyLocked(AdminCredentials& out) const;

    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    AdminCredentials _creds;
};
