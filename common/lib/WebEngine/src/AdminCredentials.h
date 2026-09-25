#pragma once
#include <stddef.h>
#include <stdint.h>

// The pure admin credential pair plus its generation counter (D3, review-3
// Should-fix 2). Not thread-safe on its own: NetEsp32's AdminAuth assigns
// it under a portMUX and verifies against a copy taken under the same lock,
// so the credentials a login was checked against and the epoch it binds its
// session to always come from the same generation. A login that verified
// the old password therefore gets the old epoch, and its session is dead as
// soon as the change lands -- it can never survive a credential change.
class AdminCredentials {
public:
    static constexpr size_t USER_MAX = 32;
    static constexpr size_t PASS_MAX = 64;

    // Copies (truncated to USER_MAX/PASS_MAX; null copies as "") and bumps
    // epoch() in the same step.
    void assign(const char* user, const char* pass);

    // Constant-time compare (both compares always run). False when the
    // stored user or password is empty (fail closed), on a null input or on
    // a mismatch. On success epochOut receives this pair's epoch; it is left
    // untouched otherwise.
    bool verify(const char* user, size_t userLen, const char* pass, size_t passLen, uint32_t& epochOut) const;

    // Password-only variant for the OTA re-entry grant (D5). Same rules.
    bool verifyPassword(const char* pass, size_t passLen, uint32_t& epochOut) const;

    uint32_t epoch() const { return _epoch; }
    const char* user() const { return _user; }
    const char* pass() const { return _pass; }

    // Overwrites both strings with zeros (volatile, not optimised away). The
    // epoch is kept.
    void wipe();

private:
    char _user[USER_MAX + 1] = {};
    char _pass[PASS_MAX + 1] = {};
    uint32_t _epoch = 0;
};

// Zeroes n bytes through a volatile pointer so the optimiser cannot drop it.
void secureWipe(void* p, size_t n);
