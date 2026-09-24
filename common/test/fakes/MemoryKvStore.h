#pragma once
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include "../../lib/CoreEngine/src/KvStore.h"

// Header-only in-memory KvStore fake for native tests, with fault injection.
// Not shipped in firmware (test-only, common/test/fakes/).
class MemoryKvStore : public KvStore {
public:
    bool failOpen = false;
    bool failWrites = false;
    bool failCommit = false;
    size_t writeCount = 0;
    size_t commitCount = 0;
    size_t eraseCount = 0;

    StoreStatus getI32(const char* key, int32_t& out) override {
        if (failOpen) return StoreStatus::OpenFailed;
        auto it = _items.find(key);
        if (it == _items.end()) return StoreStatus::NotFound;
        if (it->second.type != ItemType::I32) return StoreStatus::TypeMismatch;
        out = it->second.i32;
        return StoreStatus::Ok;
    }

    StoreStatus setI32(const char* key, int32_t value) override {
        StoreStatus s = checkWrite(key);
        if (s != StoreStatus::Ok) return s;
        Item& item = _items[key];
        item.type = ItemType::I32;
        item.i32 = value;
        ++writeCount;
        return StoreStatus::Ok;
    }

    StoreStatus getFloat(const char* key, float& out) override {
        if (failOpen) return StoreStatus::OpenFailed;
        auto it = _items.find(key);
        if (it == _items.end()) return StoreStatus::NotFound;
        if (it->second.type != ItemType::Float) return StoreStatus::TypeMismatch;
        out = it->second.f;
        return StoreStatus::Ok;
    }

    StoreStatus setFloat(const char* key, float value) override {
        StoreStatus s = checkWrite(key);
        if (s != StoreStatus::Ok) return s;
        Item& item = _items[key];
        item.type = ItemType::Float;
        item.f = value;
        ++writeCount;
        return StoreStatus::Ok;
    }

    StoreStatus getStr(const char* key, char* buf, size_t cap) override {
        if (failOpen) return StoreStatus::OpenFailed;
        auto it = _items.find(key);
        if (it == _items.end()) return StoreStatus::NotFound;
        if (it->second.type != ItemType::Str) return StoreStatus::TypeMismatch;
        if (it->second.str.size() + 1 > cap) return StoreStatus::TooLarge;
        memcpy(buf, it->second.str.c_str(), it->second.str.size() + 1);
        return StoreStatus::Ok;
    }

    StoreStatus setStr(const char* key, const char* value) override {
        StoreStatus s = checkWrite(key);
        if (s != StoreStatus::Ok) return s;
        Item& item = _items[key];
        item.type = ItemType::Str;
        item.str = value;
        ++writeCount;
        return StoreStatus::Ok;
    }

    StoreStatus getBlob(const char* key, void* buf, size_t len) override {
        if (failOpen) return StoreStatus::OpenFailed;
        auto it = _items.find(key);
        if (it == _items.end()) return StoreStatus::NotFound;
        if (it->second.type != ItemType::Blob) return StoreStatus::TypeMismatch;
        if (it->second.bytes.size() != len) return StoreStatus::TooLarge;
        memcpy(buf, it->second.bytes.data(), len);
        return StoreStatus::Ok;
    }

    StoreStatus setBlob(const char* key, const void* data, size_t len) override {
        StoreStatus s = checkWrite(key);
        if (s != StoreStatus::Ok) return s;
        Item& item = _items[key];
        item.type = ItemType::Blob;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        item.bytes.assign(bytes, bytes + len);
        ++writeCount;
        return StoreStatus::Ok;
    }

    StoreStatus remove(const char* key) override {
        if (failOpen) return StoreStatus::OpenFailed;
        auto it = _items.find(key);
        if (it == _items.end()) return StoreStatus::NotFound;
        _items.erase(it);
        return StoreStatus::Ok;
    }

    StoreStatus eraseAll() override {
        if (failOpen) return StoreStatus::OpenFailed;
        _items.clear();
        ++eraseCount;
        return StoreStatus::Ok;
    }

    StoreStatus commit() override {
        if (failOpen) return StoreStatus::OpenFailed;
        if (failCommit) return StoreStatus::WriteFailed;
        ++commitCount;
        return StoreStatus::Ok;
    }

    bool hasAnyKey() override { return !_items.empty(); }

    // Test-only: injects raw bytes for a key as a Blob, bypassing the write fault
    // flags and key-length check, to build torn/corrupt persisted data.
    void rawSetBlob(const char* key, const void* data, size_t len) {
        Item& item = _items[key];
        item.type = ItemType::Blob;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        item.bytes.assign(bytes, bytes + len);
    }

private:
    enum class ItemType : uint8_t { I32, Float, Str, Blob };
    struct Item {
        ItemType type = ItemType::I32;
        int32_t i32 = 0;
        float f = 0.0f;
        std::string str;
        std::vector<uint8_t> bytes;
    };

    StoreStatus checkWrite(const char* key) {
        if (failOpen) return StoreStatus::OpenFailed;
        if (strlen(key) > NVS_KEY_MAX_LEN) return StoreStatus::WriteFailed;
        if (failWrites) return StoreStatus::WriteFailed;
        return StoreStatus::Ok;
    }

    std::map<std::string, Item> _items;
};
