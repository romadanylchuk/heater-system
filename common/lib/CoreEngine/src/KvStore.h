#pragma once
#include <stddef.h>
#include <stdint.h>

// Key/value storage interface, bound to ONE namespace. Implementations live in
// CoreEsp32 (NVS) for firmware and common/test/fakes (in-memory) for native tests.
// CoreEngine never talks to storage directly; it only depends on this interface.
enum class StoreStatus : uint8_t {
    Ok,
    NotFound,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    TooLarge,
    TypeMismatch,
};

class KvStore {
public:
    virtual ~KvStore() = default;

    virtual StoreStatus getI32(const char* key, int32_t& out) = 0;
    virtual StoreStatus setI32(const char* key, int32_t value) = 0;

    virtual StoreStatus getFloat(const char* key, float& out) = 0;
    virtual StoreStatus setFloat(const char* key, float value) = 0;

    // TooLarge if the stored length + 1 (NUL) exceeds cap.
    virtual StoreStatus getStr(const char* key, char* buf, size_t cap) = 0;
    virtual StoreStatus setStr(const char* key, const char* value) = 0;

    // TooLarge/TypeMismatch if the stored blob size differs from len.
    virtual StoreStatus getBlob(const char* key, void* buf, size_t len) = 0;
    virtual StoreStatus setBlob(const char* key, const void* data, size_t len) = 0;

    virtual StoreStatus remove(const char* key) = 0;   // NotFound is not an error for callers
    virtual StoreStatus eraseAll() = 0;                // this namespace only
    virtual StoreStatus commit() = 0;
    virtual bool hasAnyKey() = 0;                      // false for an empty namespace
};

constexpr size_t NVS_KEY_MAX_LEN = 15;
