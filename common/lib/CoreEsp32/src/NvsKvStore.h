#pragma once
#include <nvs.h>
#include "KvStore.h"

// KvStore implementation over the ESP-IDF nvs.h API, bound to one namespace (D5).
// The constructor touches no hardware; open() performs the actual nvs_open().
// Arduino-ESP32's initArduino() already calls nvs_flash_init() -- this class never
// calls nvs_flash_init()/nvs_flash_erase().
class NvsKvStore : public KvStore {
public:
    explicit NvsKvStore(const char* ns);
    ~NvsKvStore() override;

    bool open();           // nvs_open(ns, NVS_READWRITE, ...); false -> every op returns OpenFailed
    bool isOpen() const { return _open; }

    StoreStatus getI32(const char* key, int32_t& out) override;
    StoreStatus setI32(const char* key, int32_t value) override;

    StoreStatus getFloat(const char* key, float& out) override;
    StoreStatus setFloat(const char* key, float value) override;

    StoreStatus getStr(const char* key, char* buf, size_t cap) override;
    StoreStatus setStr(const char* key, const char* value) override;

    StoreStatus getBlob(const char* key, void* buf, size_t len) override;
    StoreStatus setBlob(const char* key, const void* data, size_t len) override;

    StoreStatus remove(const char* key) override;
    StoreStatus eraseAll() override;   // nvs_erase_all(handle) -- namespace-scoped only
    StoreStatus commit() override;
    bool hasAnyKey() override;

private:
    static StoreStatus mapError(esp_err_t err, bool isWrite);

    const char* _ns;
    nvs_handle_t _handle = 0;
    bool _open = false;
};

constexpr const char* NVS_NS_CONFIG = "cfg";
constexpr const char* NVS_NS_EVENTS = "evlog";
