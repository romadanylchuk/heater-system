#include "SessionGate.h"
#include <CookieParse.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <string.h>

SessionGate::SessionGate(const AdminAuth& auth) : _auth(auth) {}

uint64_t SessionGate::nowMs() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

void SessionGate::fillRandom(uint8_t* out, size_t len) {
    esp_fill_random(out, len);  // hardware RNG (RF on in STA/AP mode)
}

bool SessionGate::headerText(AsyncWebServerRequest* r, const char* name, char* out, size_t cap) {
    out[0] = '\0';
    const String& v = r->header(name);
    size_t len = v.length();
    if (len == 0 || len >= cap) {
        return false;  // absent, or longer than any valid value: never truncated into a match
    }
    memcpy(out, v.c_str(), len);
    out[len] = '\0';
    return true;
}

bool SessionGate::sidOf(AsyncWebServerRequest* r, char* out, size_t cap) const {
    out[0] = '\0';
    const String& cookie = r->header("Cookie");
    if (cookie.length() == 0) {
        return false;
    }
    return findCookie(cookie.c_str(), SESSION_COOKIE, out, cap) && out[0] != '\0';
}

GateResult SessionGate::check(AsyncWebServerRequest* r, Access level) const {
    if (level == Access::Public) {
        return GateResult::Ok;
    }
    if (r == nullptr) {
        return GateResult::NoSession;
    }
    char sid[TOKEN_HEX_LEN + 1];
    if (!sidOf(r, sid, sizeof(sid))) {
        return GateResult::NoSession;
    }
    char csrf[TOKEN_HEX_LEN + 1];
    bool haveCsrf = level != Access::Read && headerText(r, CSRF_HEADER, csrf, sizeof(csrf));
    uint32_t epoch = _auth.epoch();  // AdminAuth's own lock, outside _mux
    uint64_t now = nowMs();
    portENTER_CRITICAL(&_mux);
    GateResult res = _store.authorize(sid, haveCsrf ? csrf : nullptr, level, now, epoch);
    portEXIT_CRITICAL(&_mux);
    return res;
}

SessionGate::LoginResult SessionGate::login(const char* user, size_t uLen, const char* pass, size_t pLen,
    SessionTokens& out, uint32_t& retryS) {
    uint64_t now = nowMs();
    portENTER_CRITICAL(&_mux);
    bool allowed = _throttle.allowed(now, retryS);
    portEXIT_CRITICAL(&_mux);
    if (!allowed) {
        return LoginResult::Throttled;
    }
    uint32_t epoch = 0;
    bool ok = _auth.verify(user, uLen, pass, pLen, &epoch);  // constant time, outside _mux
    now = nowMs();
    portENTER_CRITICAL(&_mux);
    if (ok) {
        _throttle.onSuccess();
        ok = _store.create(now, epoch, out);  // bound to the epoch the check saw
    } else {
        _throttle.onFailure(now);
    }
    portEXIT_CRITICAL(&_mux);
    return ok ? LoginResult::Ok : LoginResult::BadCredentials;
}

void SessionGate::logout(AsyncWebServerRequest* r) {
    char sid[TOKEN_HEX_LEN + 1];
    if (r == nullptr || !sidOf(r, sid, sizeof(sid))) {
        return;
    }
    portENTER_CRITICAL(&_mux);
    _store.revoke(sid);
    portEXIT_CRITICAL(&_mux);
}

SessionGate::LoginResult SessionGate::grantOta(AsyncWebServerRequest* r, const char* pass, size_t pLen,
    uint32_t& retryS) {
    char sid[TOKEN_HEX_LEN + 1];
    if (r == nullptr || !sidOf(r, sid, sizeof(sid))) {
        return LoginResult::BadCredentials;
    }
    uint64_t now = nowMs();
    portENTER_CRITICAL(&_mux);
    bool allowed = _throttle.allowed(now, retryS);
    portEXIT_CRITICAL(&_mux);
    if (!allowed) {
        return LoginResult::Throttled;
    }
    uint32_t epoch = 0;
    bool ok = _auth.verifyPassword(pass, pLen, &epoch);
    now = nowMs();
    portENTER_CRITICAL(&_mux);
    if (ok) {
        _throttle.onSuccess();
        // Only a session of the verified generation can be granted.
        ok = _store.grantOta(sid, now, epoch);
    } else {
        _throttle.onFailure(now);
    }
    portEXIT_CRITICAL(&_mux);
    return ok ? LoginResult::Ok : LoginResult::BadCredentials;
}

bool SessionGate::sessionInfo(AsyncWebServerRequest* r, char* csrfOut, size_t cap, uint32_t& ttlS,
    bool& ota) const {
    char sid[TOKEN_HEX_LEN + 1];
    if (r == nullptr || !sidOf(r, sid, sizeof(sid))) {
        return false;
    }
    uint32_t epoch = _auth.epoch();
    uint64_t now = nowMs();
    portENTER_CRITICAL(&_mux);
    bool ok = _store.csrfFor(sid, now, epoch, csrfOut, cap);
    if (ok) {
        ttlS = _store.remainingS(sid, now, epoch);
        ota = _store.otaGranted(sid, now, epoch);
    }
    portEXIT_CRITICAL(&_mux);
    return ok;
}
