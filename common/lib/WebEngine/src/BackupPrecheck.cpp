#include "BackupPrecheck.h"

#include <ArduinoJson.h>
#include <string.h>
#include <BackupCodec.h>

PrecheckResult precheckBackup(const char* json, size_t len, const char* expectedType, char* gotType, size_t cap) {
    if (gotType != nullptr && cap > 0) gotType[0] = '\0';
    if (json == nullptr || len == 0) return PrecheckResult::Empty;
    if (len > BACKUP_MAX_BYTES) return PrecheckResult::TooLarge;

    JsonDocument filter;
    filter["type"] = true;
    filter["format"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
    if (err) return PrecheckResult::Corrupt;
    if (!doc.is<JsonObject>()) return PrecheckResult::MissingType;

    JsonVariant typeVar = doc["type"];
    if (!typeVar.is<const char*>()) return PrecheckResult::MissingType;
    const char* type = typeVar.as<const char*>();
    if (gotType != nullptr && cap > 0) {
        size_t n = strlen(type);
        if (n >= cap) n = cap - 1;
        memcpy(gotType, type, n);
        gotType[n] = '\0';
    }
    if (expectedType == nullptr || strcmp(type, expectedType) != 0) return PrecheckResult::WrongType;

    JsonVariant formatVar = doc["format"];
    if (!formatVar.is<uint32_t>() || formatVar.as<uint32_t>() != BACKUP_FORMAT) {
        return PrecheckResult::UnsupportedFormat;
    }
    return PrecheckResult::Ok;
}

const char* precheckKey(PrecheckResult r) {
    switch (r) {
        case PrecheckResult::Ok:                return "ok";
        case PrecheckResult::Empty:             return "empty";
        case PrecheckResult::TooLarge:          return "too_large";
        case PrecheckResult::Corrupt:           return "corrupt";
        case PrecheckResult::MissingType:       return "missing_type";
        case PrecheckResult::WrongType:         return "wrong_type";
        case PrecheckResult::UnsupportedFormat: return "unsupported_format";
    }
    return "corrupt";
}
