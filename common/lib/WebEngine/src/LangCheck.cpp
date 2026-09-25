#include "LangCheck.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

void copyKey(const char* key, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return;
    if (key == nullptr) key = "";
    size_t n = strlen(key);
    if (n >= cap) n = cap - 1;
    memcpy(out, key, n);
    out[n] = '\0';
}

// Every value must be a non-empty string. Returns Ok or the first failure.
LangCheckResult checkFlat(JsonObjectConst obj, char* firstKey, size_t cap) {
    for (JsonPairConst kv : obj) {
        JsonVariantConst v = kv.value();
        if (!v.is<const char*>()) {
            copyKey(kv.key().c_str(), firstKey, cap);
            return LangCheckResult::NotObject;
        }
        const char* s = v.as<const char*>();
        if (s == nullptr || s[0] == '\0') {
            copyKey(kv.key().c_str(), firstKey, cap);
            return LangCheckResult::EmptyValue;
        }
    }
    return LangCheckResult::Ok;
}

// First key of `from` that `in` does not contain, or null.
const char* firstMissing(JsonObjectConst from, JsonObjectConst in) {
    for (JsonPairConst kv : from) {
        if (!in[kv.key()].is<const char*>()) return kv.key().c_str();
    }
    return nullptr;
}

}  // namespace

LangCheckResult compareLangJson(const char* a, size_t aLen, const char* b, size_t bLen, char* firstKey, size_t cap) {
    copyKey("", firstKey, cap);
    if (a == nullptr || aLen == 0) return LangCheckResult::ParseErrorA;
    if (b == nullptr || bLen == 0) return LangCheckResult::ParseErrorB;

    JsonDocument docA;
    if (deserializeJson(docA, a, aLen)) return LangCheckResult::ParseErrorA;
    JsonDocument docB;
    if (deserializeJson(docB, b, bLen)) return LangCheckResult::ParseErrorB;
    if (!docA.is<JsonObjectConst>() || !docB.is<JsonObjectConst>()) return LangCheckResult::NotObject;

    JsonObjectConst objA = docA.as<JsonObjectConst>();
    JsonObjectConst objB = docB.as<JsonObjectConst>();

    LangCheckResult r = checkFlat(objA, firstKey, cap);
    if (r != LangCheckResult::Ok) return r;
    r = checkFlat(objB, firstKey, cap);
    if (r != LangCheckResult::Ok) return r;

    const char* missing = firstMissing(objA, objB);
    if (missing != nullptr) {
        copyKey(missing, firstKey, cap);
        return LangCheckResult::MissingInB;
    }
    missing = firstMissing(objB, objA);
    if (missing != nullptr) {
        copyKey(missing, firstKey, cap);
        return LangCheckResult::MissingInA;
    }
    return LangCheckResult::Ok;
}

const char* langCheckKey(LangCheckResult r) {
    switch (r) {
        case LangCheckResult::Ok:          return "ok";
        case LangCheckResult::ParseErrorA: return "parse_error_a";
        case LangCheckResult::ParseErrorB: return "parse_error_b";
        case LangCheckResult::NotObject:   return "not_object";
        case LangCheckResult::MissingInA:  return "missing_in_a";
        case LangCheckResult::MissingInB:  return "missing_in_b";
        case LangCheckResult::EmptyValue:  return "empty_value";
    }
    return "not_object";
}
