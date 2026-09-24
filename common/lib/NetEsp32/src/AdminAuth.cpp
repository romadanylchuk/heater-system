#include "AdminAuth.h"
#include <ESPAsyncWebServer.h>
#include <string.h>

namespace {

void copyTrunc(char* out, size_t cap, const char* in) {
    size_t len = in != nullptr ? strnlen(in, cap - 1) : 0;
    memcpy(out, in != nullptr ? in : "", len);
    out[len] = '\0';
}

}  // namespace

void AdminAuth::set(const char* user, const char* pass) {
    char u[USER_MAX + 1];
    char p[PASS_MAX + 1];
    copyTrunc(u, sizeof(u), user);
    copyTrunc(p, sizeof(p), pass);
    portENTER_CRITICAL(&_mux);
    memcpy(_user, u, sizeof(_user));
    memcpy(_pass, p, sizeof(_pass));
    portEXIT_CRITICAL(&_mux);
}

bool AdminAuth::check(AsyncWebServerRequest* r) const {
    if (r == nullptr) {
        return false;
    }
    char u[USER_MAX + 1];
    char p[PASS_MAX + 1];
    portENTER_CRITICAL(&_mux);
    memcpy(u, _user, sizeof(u));
    memcpy(p, _pass, sizeof(p));
    portEXIT_CRITICAL(&_mux);
    if (u[0] == '\0' || p[0] == '\0') {
        return false;  // fail closed: never accept an empty credential pair
    }
    bool ok = r->authenticate(u, p);
    memset(p, 0, sizeof(p));
    return ok;
}

void AdminAuth::challenge(AsyncWebServerRequest* r) const {
    if (r != nullptr) {
        r->requestAuthentication();
    }
}
