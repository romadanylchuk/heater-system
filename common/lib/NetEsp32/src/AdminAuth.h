#pragma once
#include <freertos/FreeRTOS.h>
#include <stddef.h>

class AsyncWebServerRequest;

// Thread-safe admin credential check (D17). The ONLY credential source for
// the stage-04 routes (ElegantOTA's /update + /ota/*, /api/wifi*,
// /api/version, /setup) and reusable by stage 05 via
// ConnectivityServices::adminAuth().
//
// set() runs on the loop task (at begin and whenever webUser/webPass change);
// check() runs on the AsyncTCP task. The credentials live in fixed char
// arrays guarded by a portMUX spinlock: check() copies them into locals under
// the lock and authenticates against the copies, so a concurrent set() can
// never tear a String/pointer the web task is reading (ElegantOTA's own
// String-based auth is deliberately not used for that reason).
class AdminAuth {
public:
    static constexpr size_t USER_MAX = 32;
    static constexpr size_t PASS_MAX = 64;

    // Copies (truncated to USER_MAX/PASS_MAX); null copies as "".
    void set(const char* user, const char* pass);

    // HTTP Basic/Digest check against a locked copy. False when no
    // credentials are set (fail closed) or on mismatch.
    bool check(AsyncWebServerRequest* r) const;

    // Sends the 401 digest challenge.
    void challenge(AsyncWebServerRequest* r) const;

private:
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    char _user[USER_MAX + 1] = {};
    char _pass[PASS_MAX + 1] = {};
};
