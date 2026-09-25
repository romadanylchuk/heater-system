#include "JsonOut.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

JsonOut::JsonOut(char* buf, size_t cap) : _buf(buf), _cap(cap) {
    if (_cap > 0) {
        _buf[0] = '\0';
    } else {
        _ok = false;
    }
}

void JsonOut::putChar(char c) {
    if (!_ok) {
        return;
    }
    if (_len + 1 >= _cap) {
        _ok = false;
        _buf[_len] = '\0';
        return;
    }
    _buf[_len++] = c;
    _buf[_len] = '\0';
}

void JsonOut::putRaw(const char* s, size_t n) {
    for (size_t i = 0; i < n && _ok; ++i) {
        putChar(s[i]);
    }
}

// Called right before writing any value (a scalar, or the '{'/'[' of a
// nested container). If the *enclosing* container is an array, this value
// is an array element and needs comma bookkeeping. If the enclosing
// container is an object, the preceding key() call already positioned the
// cursor after ':', so nothing is needed here.
void JsonOut::beforeValue() {
    if (_depth == 0) {
        return;
    }
    if (_stack[_depth - 1] == Ctx::Arr) {
        if (_hasItem[_depth - 1]) {
            putChar(',');
        }
        _hasItem[_depth - 1] = true;
    }
}

void JsonOut::beforeContainer() {
    beforeValue();
}

void JsonOut::pushFrame(Ctx c) {
    if (_depth >= MAX_DEPTH) {
        _ok = false;
        return;
    }
    _stack[_depth] = c;
    _hasItem[_depth] = false;
    ++_depth;
}

void JsonOut::popFrame(char closer) {
    if (_depth == 0) {
        _ok = false;
        return;
    }
    --_depth;
    putChar(closer);
}

void JsonOut::beginObject() {
    beforeContainer();
    pushFrame(Ctx::Obj);
    putChar('{');
}

void JsonOut::endObject() {
    popFrame('}');
}

void JsonOut::beginArray() {
    beforeContainer();
    pushFrame(Ctx::Arr);
    putChar('[');
}

void JsonOut::endArray() {
    popFrame(']');
}

void JsonOut::key(const char* k) {
    if (_depth > 0 && _hasItem[_depth - 1]) {
        putChar(',');
    }
    if (_depth > 0) {
        _hasItem[_depth - 1] = true;
    }
    putChar('"');
    if (k != nullptr) {
        putRaw(k, strlen(k));
    }
    putChar('"');
    putChar(':');
}

void JsonOut::putEscaped(const char* v) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(v); *p != '\0'; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            putChar('\\');
            putChar(static_cast<char>(c));
        } else if (c < 0x20) {
            char buf[7];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            for (size_t i = 0; buf[i] != '\0'; ++i) {
                putChar(buf[i]);
            }
        } else {
            putChar(static_cast<char>(c));
        }
    }
}

void JsonOut::str(const char* v) {
    beforeValue();
    if (v == nullptr) {
        putRaw("null", 4);
        return;
    }
    putChar('"');
    putEscaped(v);
    putChar('"');
}

void JsonOut::num(float v, uint8_t decimals) {
    beforeValue();
    if (isnan(v) || isinf(v)) {
        putRaw("null", 4);
        return;
    }
    char tmp[40];
    int n = snprintf(tmp, sizeof(tmp), "%.*f", decimals, static_cast<double>(v));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(tmp)) {
        putRaw("null", 4);
        return;
    }
    if (decimals > 0) {
        // Trim trailing zeros in the fraction, then the '.' itself if the
        // whole fraction was zero.
        int end = n - 1;
        while (end > 0 && tmp[end] == '0') {
            --end;
        }
        if (tmp[end] == '.') {
            --end;
        }
        n = end + 1;
    }
    putRaw(tmp, static_cast<size_t>(n));
}

void JsonOut::integer(int64_t v) {
    beforeValue();
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), "%lld", static_cast<long long>(v));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(tmp)) {
        putRaw("null", 4);
        return;
    }
    putRaw(tmp, static_cast<size_t>(n));
}

void JsonOut::boolean(bool v) {
    beforeValue();
    putRaw(v ? "true" : "false", v ? 4 : 5);
}

void JsonOut::null() {
    beforeValue();
    putRaw("null", 4);
}

void JsonOut::raw(const char* json) {
    beforeValue();
    if (json == nullptr) {
        putRaw("null", 4);
        return;
    }
    putRaw(json, strlen(json));
}
