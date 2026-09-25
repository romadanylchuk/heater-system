#pragma once
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/WebEngine/src/LangCheck.h"

// Native tests for the language-file parity check (stage 05 phase 6, D13):
// synthetic cases for every LangCheckResult, then the real
// common/web/lang/en.json and uk.json. Test names are prefixed web_lang_.

namespace {

LangCheckResult webLangCompare(const char* a, const char* b, char* key, size_t cap) {
    return compareLangJson(a, strlen(a), b, strlen(b), key, cap);
}

void webLangExpect(LangCheckResult expected, const char* a, const char* b, const char* expectedKey) {
    char key[32];
    memset(key, 'x', sizeof(key));
    LangCheckResult r = webLangCompare(a, b, key, sizeof(key));
    TEST_ASSERT_EQUAL_STRING(langCheckKey(expected), langCheckKey(r));
    TEST_ASSERT_EQUAL_STRING(expectedKey, key);
}

// Reads a whole file; false if it cannot be opened.
bool webLangReadFile(const char* path, std::string& out) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    out.clear();
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// pio runs the test binary from the project dir (boiler-room/ or
// home-heating/); the second prefix covers a run from the repo root.
const char* const WEB_LANG_PREFIXES[] = {"../common/web/lang/", "common/web/lang/"};

bool webLangLoad(const char* name, std::string& out) {
    for (const char* prefix : WEB_LANG_PREFIXES) {
        std::string path = std::string(prefix) + name;
        if (webLangReadFile(path.c_str(), out)) return true;
    }
    return false;
}

}  // namespace

static void web_lang_test_ok_same_keys_any_order() {
    webLangExpect(LangCheckResult::Ok, "{\"a\":\"A\",\"b\":\"B\"}", "{\"b\":\"Б\",\"a\":\"А\"}", "");
    webLangExpect(LangCheckResult::Ok, "{}", "{}", "");
}

static void web_lang_test_parse_errors() {
    webLangExpect(LangCheckResult::ParseErrorA, "{\"a\":", "{\"a\":\"A\"}", "");
    webLangExpect(LangCheckResult::ParseErrorB, "{\"a\":\"A\"}", "{\"a\" \"A\"}", "");
    webLangExpect(LangCheckResult::ParseErrorA, "", "{}", "");
    char key[8] = "zz";
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LangCheckResult::ParseErrorA),
        static_cast<int>(compareLangJson(nullptr, 0, "{}", 2, key, sizeof(key))));
    TEST_ASSERT_EQUAL_STRING("", key);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LangCheckResult::ParseErrorB),
        static_cast<int>(compareLangJson("{}", 2, nullptr, 0, key, sizeof(key))));
}

static void web_lang_test_not_object_and_not_flat() {
    webLangExpect(LangCheckResult::NotObject, "[\"a\"]", "{}", "");
    webLangExpect(LangCheckResult::NotObject, "{}", "\"text\"", "");
    webLangExpect(LangCheckResult::NotObject, "{\"a\":{\"b\":\"B\"}}", "{\"a\":\"A\"}", "a");
    webLangExpect(LangCheckResult::NotObject, "{\"a\":\"A\"}", "{\"a\":5}", "a");
    webLangExpect(LangCheckResult::NotObject, "{\"a\":\"A\",\"n\":null}", "{\"a\":\"A\",\"n\":\"N\"}", "n");
}

static void web_lang_test_missing_keys() {
    webLangExpect(LangCheckResult::MissingInB, "{\"a\":\"A\",\"b\":\"B\"}", "{\"a\":\"A\"}", "b");
    webLangExpect(LangCheckResult::MissingInA, "{\"a\":\"A\"}", "{\"a\":\"A\",\"c\":\"C\"}", "c");
    // Key names are compared exactly (case-sensitive).
    webLangExpect(LangCheckResult::MissingInB, "{\"Ab\":\"x\"}", "{\"ab\":\"x\"}", "Ab");
}

static void web_lang_test_empty_value() {
    webLangExpect(LangCheckResult::EmptyValue, "{\"a\":\"\"}", "{\"a\":\"A\"}", "a");
    webLangExpect(LangCheckResult::EmptyValue, "{\"a\":\"A\",\"b\":\"B\"}", "{\"a\":\"A\",\"b\":\"\"}", "b");
}

static void web_lang_test_key_truncation_and_null_out() {
    char key[4];
    LangCheckResult r = webLangCompare("{\"longkey\":\"x\"}", "{}", key, sizeof(key));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LangCheckResult::MissingInB), static_cast<int>(r));
    TEST_ASSERT_EQUAL_STRING("lon", key);
    r = webLangCompare("{\"a\":\"A\"}", "{}", nullptr, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(LangCheckResult::MissingInB), static_cast<int>(r));
}

static void web_lang_test_real_files_parity_and_coverage() {
    std::string en;
    std::string uk;
    if (!webLangLoad("en.json", en) || !webLangLoad("uk.json", uk)) {
        TEST_FAIL_MESSAGE("common/web/lang/en.json or uk.json not found from the test working directory");
        return;
    }
    char key[64];
    LangCheckResult r = compareLangJson(en.data(), en.size(), uk.data(), uk.size(), key, sizeof(key));
    if (r != LangCheckResult::Ok) {
        std::string msg = std::string("lang parity: ") + langCheckKey(r) + " key=" + key;
        TEST_FAIL_MESSAGE(msg.c_str());
    }

    // Every common event type and every EventReason has a label.
    for (uint16_t type = 0; type <= 40; ++type) {
        const char* evKey = eventTypeKey(type);
        if (evKey == nullptr) continue;
        std::string needle = std::string("\"ev.") + evKey + "\"";
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(en.c_str(), needle.c_str()), needle.c_str());
    }
    for (int rsn = 0; rsn <= static_cast<int>(EventReason::Migration); ++rsn) {
        std::string needle = "\"rsn." + std::to_string(rsn) + "\"";
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(en.c_str(), needle.c_str()), needle.c_str());
    }
}

inline void runWebLangSuite() {
    RUN_TEST(web_lang_test_ok_same_keys_any_order);
    RUN_TEST(web_lang_test_parse_errors);
    RUN_TEST(web_lang_test_not_object_and_not_flat);
    RUN_TEST(web_lang_test_missing_keys);
    RUN_TEST(web_lang_test_empty_value);
    RUN_TEST(web_lang_test_key_truncation_and_null_out);
    RUN_TEST(web_lang_test_real_files_parity_and_coverage);
}
