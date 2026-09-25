#include "AdminAuth.h"
#include <string.h>

namespace {

void copyTrunc(char* out, size_t cap, const char* in) {
    size_t len = in != nullptr ? strnlen(in, cap - 1) : 0;
    memcpy(out, in != nullptr ? in : "", len);
    out[len] = '\0';
}

}  // namespace

void AdminAuth::set(const char* user, const char* pass) {
    // Truncated local copies first, so the critical section only does two
    // bounded copies and the epoch bump (review-3 Should-fix 2: the bump is
    // now inside the lock, atomic with the new pair).
    char u[USER_MAX + 1];
    char p[PASS_MAX + 1];
    copyTrunc(u, sizeof(u), user);
    copyTrunc(p, sizeof(p), pass);
    portENTER_CRITICAL(&_mux);
    _creds.assign(u, p);
    portEXIT_CRITICAL(&_mux);
    secureWipe(u, sizeof(u));
    secureWipe(p, sizeof(p));
}

void AdminAuth::copyLocked(AdminCredentials& out) const {
    portENTER_CRITICAL(&_mux);
    out = _creds;
    portEXIT_CRITICAL(&_mux);
}

uint32_t AdminAuth::epoch() const {
    portENTER_CRITICAL(&_mux);
    uint32_t e = _creds.epoch();
    portEXIT_CRITICAL(&_mux);
    return e;
}

bool AdminAuth::verify(const char* user, size_t userLen, const char* pass, size_t passLen,
    uint32_t* epochOut) const {
    if (user == nullptr || pass == nullptr) {
        return false;
    }
    AdminCredentials copy;
    copyLocked(copy);
    uint32_t e = 0;
    bool ok = copy.verify(user, userLen, pass, passLen, e);
    copy.wipe();
    if (ok && epochOut != nullptr) {
        *epochOut = e;
    }
    return ok;
}

bool AdminAuth::verifyPassword(const char* pass, size_t passLen, uint32_t* epochOut) const {
    if (pass == nullptr) {
        return false;
    }
    AdminCredentials copy;
    copyLocked(copy);
    uint32_t e = 0;
    bool ok = copy.verifyPassword(pass, passLen, e);
    copy.wipe();
    if (ok && epochOut != nullptr) {
        *epochOut = e;
    }
    return ok;
}
