#pragma once
#include <freertos/FreeRTOS.h>
#include <AdminAuth.h>
#include <LoginThrottle.h>
#include <RequestGate.h>
#include <SessionStore.h>

// The stage-05 RequestGate (D2-D6): session cookie + CSRF synchronizer token
// + OTA re-entry grant over the pure SessionStore, and the global login
// throttle. Every method runs on the AsyncTCP task; the store and throttle
// are guarded by one portMUX spinlock, and request header texts are copied
// into stack buffers BEFORE the critical section (no String/heap work while
// the lock is held). Password checks (AdminAuth, which has its own lock) run
// outside _mux; the new session is bound to the epoch the check reported
// (review-3 Should-fix 2), never to a separate epoch() read.
class SessionGate : public RequestGate {
public:
    enum class LoginResult : uint8_t { Ok, BadCredentials, Throttled };

    explicit SessionGate(const AdminAuth& auth);

    GateResult check(AsyncWebServerRequest* r, Access level) const override;

    // throttle -> auth.verify -> store.create. retryS is set when Throttled.
    LoginResult login(const char* user, size_t uLen, const char* pass, size_t pLen, SessionTokens& out,
        uint32_t& retryS);

    // Revokes the request's session (no-op without one).
    void logout(AsyncWebServerRequest* r);

    // The caller has already passed check(r, Write). throttle ->
    // auth.verifyPassword -> store.grantOta for the request's session.
    LoginResult grantOta(AsyncWebServerRequest* r, const char* pass, size_t pLen, uint32_t& retryS);

    // For GET /api/session: false without a valid session.
    bool sessionInfo(AsyncWebServerRequest* r, char* csrfOut, size_t cap, uint32_t& ttlS, bool& ota) const;

private:
    static uint64_t nowMs();                                                // esp_timer_get_time() / 1000
    static void fillRandom(uint8_t* out, size_t len);                      // esp_fill_random
    static bool headerText(AsyncWebServerRequest* r, const char* name, char* out, size_t cap);
    bool sidOf(AsyncWebServerRequest* r, char* out, size_t cap) const;     // Cookie header -> findCookie

    const AdminAuth& _auth;
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    mutable SessionStore _store{&SessionGate::fillRandom};
    LoginThrottle _throttle;
};
