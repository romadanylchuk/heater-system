#pragma once
#include <stddef.h>

// Constant-time buffer comparison for tokens/passwords (D4/AdminAuth). A
// length mismatch still returns false, but the loop always runs over
// max(aLen, bLen) bytes so the timing does not leak which input was longer
// or where the first mismatch was.
inline bool constTimeEquals(const char* a, size_t aLen, const char* b, size_t bLen) {
    size_t n = aLen > bLen ? aLen : bLen;
    unsigned char diff = static_cast<unsigned char>(aLen != bLen);
    for (size_t i = 0; i < n; ++i) {
        unsigned char ca = i < aLen ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = i < bLen ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(ca ^ cb);
    }
    return diff == 0;
}
