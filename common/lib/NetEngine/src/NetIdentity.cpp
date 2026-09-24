#include "NetIdentity.h"
#include <string.h>

namespace {

bool isPrefixChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }

bool isUniquePrefixChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

}  // namespace

bool validateNetIdentity(const NetIdentity& id) {
    if (!id.prefix || !id.uniquePrefix || !id.deviceName || !id.model || !id.apSsid) {
        return false;
    }

    const size_t prefixLen = strlen(id.prefix);
    if (prefixLen == 0 || prefixLen > NET_PREFIX_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < prefixLen; ++i) {
        if (!isPrefixChar(id.prefix[i])) {
            return false;
        }
    }
    if (strcmp(id.prefix, "boiler") == 0 || strcmp(id.prefix, "home") == 0) {
        return false;
    }

    const size_t uniqueLen = strlen(id.uniquePrefix);
    if (uniqueLen == 0 || uniqueLen > NET_PREFIX_MAX_LEN) {
        return false;
    }
    for (size_t i = 0; i < uniqueLen; ++i) {
        if (!isUniquePrefixChar(id.uniquePrefix[i])) {
            return false;
        }
    }

    if (id.deviceName[0] == '\0') {
        return false;
    }
    if (id.model[0] == '\0') {
        return false;
    }

    const size_t apSsidLen = strlen(id.apSsid);
    if (apSsidLen == 0 || apSsidLen > 32) {
        return false;
    }

    return true;
}

bool toSnakeKey(const char* in, char* out, size_t cap) {
    if (!in || !out || cap == 0) {
        return false;
    }

    // Built into a bounded local buffer first, so `out` is left untouched on
    // any failure path (empty result, overflow of `cap`, or a pathological
    // input longer than any real setting/entity key could ever be).
    constexpr size_t TMP_CAP = 96;
    char tmp[TMP_CAP];
    size_t o = 0;
    bool prevLowerOrDigit = false;

    for (const char* p = in; *p != '\0'; ++p) {
        const char c = *p;
        char emit;
        bool insertUnderscoreBefore = false;

        if (c >= 'A' && c <= 'Z') {
            emit = static_cast<char>(c - 'A' + 'a');
            insertUnderscoreBefore = prevLowerOrDigit;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            emit = c;
        } else if (c == ' ' || c == '-' || c == '_') {
            emit = '_';
        } else {
            continue;  // dropped: not in [a-z0-9_] after normalisation
        }

        if (insertUnderscoreBefore) {
            if (o + 1 >= TMP_CAP) {
                return false;
            }
            tmp[o++] = '_';
        }
        if (o + 1 >= TMP_CAP) {
            return false;
        }
        tmp[o++] = emit;
        prevLowerOrDigit = (emit >= 'a' && emit <= 'z') || (emit >= '0' && emit <= '9');
    }

    if (o == 0) {
        return false;
    }
    if (o + 1 > cap) {
        return false;
    }

    memcpy(out, tmp, o);
    out[o] = '\0';
    return true;
}
