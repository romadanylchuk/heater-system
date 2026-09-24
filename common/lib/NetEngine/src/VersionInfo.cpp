#include "VersionInfo.h"
#include <string.h>

namespace {

bool isTrimmable(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool isPrintable(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x20 && u <= 0x7E;
}

void copyTrunc(char* dst, size_t cap, const char* src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (cap == 0) {
        return;
    }
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

bool parseWebVersionText(const char* raw, size_t len, char* out, size_t cap) {
    if (!raw || !out || cap == 0) {
        return false;
    }

    size_t start = 0;
    while (start < len && isTrimmable(raw[start])) {
        ++start;
    }
    size_t end = len;
    while (end > start && isTrimmable(raw[end - 1])) {
        --end;
    }
    if (end <= start) {
        return false;
    }

    for (size_t i = start; i < end; ++i) {
        if (!isPrintable(raw[i])) {
            return false;
        }
    }

    size_t n = end - start;
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(out, raw + start, n);
    out[n] = '\0';
    return true;
}

void fillVersionStatus(VersionStatus& out, const char* fw, const char* web) {
    copyTrunc(out.fw, sizeof(out.fw), fw);
    copyTrunc(out.web, sizeof(out.web), web);
    out.webMismatch = out.web[0] != '\0' && strcmp(out.fw, out.web) != 0;
}
