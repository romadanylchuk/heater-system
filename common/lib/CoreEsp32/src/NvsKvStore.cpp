#include "NvsKvStore.h"
#include <string.h>

NvsKvStore::NvsKvStore(const char* ns) : _ns(ns) {}

NvsKvStore::~NvsKvStore() {
    if (_open) {
        nvs_close(_handle);
    }
}

StoreStatus NvsKvStore::mapError(esp_err_t err, bool isWrite) {
    switch (err) {
        case ESP_OK:
            return StoreStatus::Ok;
        case ESP_ERR_NVS_NOT_FOUND:
            return StoreStatus::NotFound;
        case ESP_ERR_NVS_INVALID_LENGTH:
            return StoreStatus::TooLarge;
        case ESP_ERR_NVS_TYPE_MISMATCH:
            return StoreStatus::TypeMismatch;
        default:
            return isWrite ? StoreStatus::WriteFailed : StoreStatus::ReadFailed;
    }
}

bool NvsKvStore::open() {
    esp_err_t err = nvs_open(_ns, NVS_READWRITE, &_handle);
    _open = (err == ESP_OK);
    return _open;
}

StoreStatus NvsKvStore::getI32(const char* key, int32_t& out) {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_get_i32(_handle, key, &out), false);
}

StoreStatus NvsKvStore::setI32(const char* key, int32_t value) {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_set_i32(_handle, key, value), true);
}

StoreStatus NvsKvStore::getFloat(const char* key, float& out) {
    if (!_open) return StoreStatus::OpenFailed;
    uint32_t bits = 0;
    StoreStatus status = mapError(nvs_get_u32(_handle, key, &bits), false);
    if (status == StoreStatus::Ok) {
        memcpy(&out, &bits, sizeof(out));
    }
    return status;
}

StoreStatus NvsKvStore::setFloat(const char* key, float value) {
    if (!_open) return StoreStatus::OpenFailed;
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return mapError(nvs_set_u32(_handle, key, bits), true);
}

StoreStatus NvsKvStore::getStr(const char* key, char* buf, size_t cap) {
    if (!_open) return StoreStatus::OpenFailed;
    size_t len = cap;
    return mapError(nvs_get_str(_handle, key, buf, &len), false);
}

StoreStatus NvsKvStore::setStr(const char* key, const char* value) {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_set_str(_handle, key, value), true);
}

StoreStatus NvsKvStore::getBlob(const char* key, void* buf, size_t len) {
    if (!_open) return StoreStatus::OpenFailed;
    size_t storedSize = 0;
    esp_err_t err = nvs_get_blob(_handle, key, nullptr, &storedSize);
    if (err != ESP_OK) {
        return mapError(err, false);
    }
    if (storedSize != len) {
        return StoreStatus::TooLarge;
    }
    return mapError(nvs_get_blob(_handle, key, buf, &storedSize), false);
}

StoreStatus NvsKvStore::setBlob(const char* key, const void* data, size_t len) {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_set_blob(_handle, key, data, len), true);
}

StoreStatus NvsKvStore::remove(const char* key) {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_erase_key(_handle, key), true);
}

StoreStatus NvsKvStore::eraseAll() {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_erase_all(_handle), true);
}

StoreStatus NvsKvStore::commit() {
    if (!_open) return StoreStatus::OpenFailed;
    return mapError(nvs_commit(_handle), true);
}

bool NvsKvStore::hasAnyKey() {
    if (!_open) return false;
    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, _ns, NVS_TYPE_ANY);
    if (it == nullptr) {
        return false;
    }
    nvs_release_iterator(it);
    return true;
}
