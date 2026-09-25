#pragma once
#include <stddef.h>
#include <stdint.h>

// A fixed-buffer, heap-free JSON writer used by every WebEngine JSON
// builder (D8). The caller drives it directly (beginObject/key/str/...); no
// schema validation is performed beyond nesting-depth and buffer-capacity
// bookkeeping. Any overflow (buffer full or nesting too deep) latches
// ok() == false for the rest of the object's lifetime; the buffer stays
// NUL-terminated throughout.
class JsonOut {
public:
    JsonOut(char* buf, size_t cap);

    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    // Emits comma handling (if the enclosing object already has a member)
    // followed by "k":. Only valid directly inside an object.
    void key(const char* k);

    // Escapes '"', '\\' and control characters (as \u00XX); passes UTF-8
    // bytes through unchanged. nullptr writes the JSON literal null.
    void str(const char* v);

    // NaN/inf write the JSON literal null. Otherwise formatted with
    // `decimals` fractional digits and trailing zeros trimmed (an all-zero
    // fraction is dropped along with the decimal point).
    void num(float v, uint8_t decimals);
    void integer(int64_t v);
    void boolean(bool v);
    void null();

    // Appends a pre-validated JSON fragment verbatim (no escaping, no comma
    // handling beyond the normal array/object item bookkeeping).
    void raw(const char* json);

    bool ok() const { return _ok; }
    size_t length() const { return _ok ? _len : 0; }

private:
    static constexpr size_t MAX_DEPTH = 8;
    enum class Ctx : uint8_t { Obj, Arr };

    char* _buf;
    size_t _cap;
    size_t _len = 0;
    bool _ok = true;
    uint8_t _depth = 0;
    Ctx _stack[MAX_DEPTH] = {};
    bool _hasItem[MAX_DEPTH] = {};

    void putChar(char c);
    void putRaw(const char* s, size_t n);
    void putEscaped(const char* s);
    void beforeContainer();  // comma bookkeeping when opening a nested object/array
    void beforeValue();      // comma bookkeeping when writing a bare value (array context)
    void pushFrame(Ctx c);
    void popFrame(char closer);
};
