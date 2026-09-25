#include "AdminCredentials.h"
#include <string.h>
#include "ConstTime.h"

namespace {

void copyTrunc(char* out, size_t cap, const char* in) {
    size_t len = in != nullptr ? strnlen(in, cap - 1) : 0;
    memcpy(out, in != nullptr ? in : "", len);
    memset(out + len, 0, cap - len);
}

}  // namespace

void secureWipe(void* p, size_t n) {
    volatile unsigned char* v = static_cast<volatile unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) {
        v[i] = 0;
    }
}

void AdminCredentials::assign(const char* user, const char* pass) {
    copyTrunc(_user, sizeof(_user), user);
    copyTrunc(_pass, sizeof(_pass), pass);
    ++_epoch;
}

bool AdminCredentials::verify(const char* user, size_t userLen, const char* pass, size_t passLen,
    uint32_t& epochOut) const {
    if (user == nullptr || pass == nullptr || _user[0] == '\0' || _pass[0] == '\0') {
        return false;  // fail closed: never accept an empty stored pair
    }
    // Evaluate both compares unconditionally (no short-circuit timing).
    bool userOk = constTimeEquals(_user, strlen(_user), user, userLen);
    bool passOk = constTimeEquals(_pass, strlen(_pass), pass, passLen);
    if (!(userOk & passOk)) {
        return false;
    }
    epochOut = _epoch;
    return true;
}

bool AdminCredentials::verifyPassword(const char* pass, size_t passLen, uint32_t& epochOut) const {
    if (pass == nullptr || _user[0] == '\0' || _pass[0] == '\0') {
        return false;
    }
    if (!constTimeEquals(_pass, strlen(_pass), pass, passLen)) {
        return false;
    }
    epochOut = _epoch;
    return true;
}

void AdminCredentials::wipe() {
    secureWipe(_user, sizeof(_user));
    secureWipe(_pass, sizeof(_pass));
}
