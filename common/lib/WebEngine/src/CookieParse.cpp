#include "CookieParse.h"
#include <stdio.h>
#include <string.h>

bool findCookie(const char* header, const char* name, char* out, size_t cap) {
    if (header == nullptr || name == nullptr || out == nullptr || cap == 0) {
        return false;
    }
    size_t nameLen = strlen(name);
    const char* p = header;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        const char* segStart = p;
        const char* semi = strchr(p, ';');
        const char* segEnd = semi != nullptr ? semi : p + strlen(p);

        const char* eq = nullptr;
        for (const char* q = segStart; q < segEnd; ++q) {
            if (*q == '=') {
                eq = q;
                break;
            }
        }
        if (eq != nullptr) {
            size_t keyLen = static_cast<size_t>(eq - segStart);
            if (keyLen == nameLen && strncmp(segStart, name, nameLen) == 0) {
                const char* valStart = eq + 1;
                const char* valEnd = segEnd;
                while (valEnd > valStart && (*(valEnd - 1) == ' ' || *(valEnd - 1) == '\t')) {
                    --valEnd;
                }
                size_t valLen = static_cast<size_t>(valEnd - valStart);
                if (valLen >= cap) {
                    return false;
                }
                memcpy(out, valStart, valLen);
                out[valLen] = '\0';
                return true;
            }
        }
        if (semi == nullptr) {
            break;
        }
        p = semi + 1;
    }
    return false;
}

size_t formatSessionCookie(const char* sid, uint32_t maxAgeS, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    int n = snprintf(out, cap, "hsid=%s; Path=/; Max-Age=%u; HttpOnly; SameSite=Strict", sid != nullptr ? sid : "",
        static_cast<unsigned int>(maxAgeS));
    if (n < 0 || static_cast<size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n);
}
