#pragma once
#include <unity.h>
#include <ArduinoJson.h>
#include <string.h>
#include "../lib/CoreEngine/src/BackupCodec.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "fakes/FakeClock.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/TestSchema.h"

// Native-safe Unity tests for BackupCodec (stage 02 phase 3): export shape, the
// two-pass import validation, clamping, migration-aware renames and version
// skew. Header-only so both project test wrappers can include and run it via
// CommonSuite.h. Test names are prefixed bak_ (D23).

static void bak_test_export_contains_expected_fields_and_excludes_no_backup() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
    engine.setText(TEST_INDEX_SECRET, "topsecret", EventReason::Web, 0);

    char buf[1024];
    size_t len = BackupCodec::exportJson(engine, "1.2.3", buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_STRING("test-a", doc["type"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(1, doc["format"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(1, doc["configVersion"].as<uint32_t>());
    TEST_ASSERT_EQUAL_STRING("1.2.3", doc["fwVersion"].as<const char*>());

    JsonObject settings = doc["settings"].as<JsonObject>();
    TEST_ASSERT_FALSE(settings["testInt"].isNull());
    TEST_ASSERT_FALSE(settings["testSecret"].isNull());
    TEST_ASSERT_EQUAL_STRING("topsecret", settings["testSecret"].as<const char*>());
    TEST_ASSERT_TRUE(settings["testNoBackup"].isNull());  // SETTING_FLAG_NO_BACKUP excluded
}

static void bak_test_export_buffer_too_small_returns_zero() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    char tiny[4];
    size_t len = BackupCodec::exportJson(engine, "1.0.0", tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_UINT32(0, len);
}

static void bak_test_round_trip_import_matches_export() {
    MemoryKvStore cfgStoreA, logStoreA;
    FakeClock clockA;
    EventLog logA(logStoreA, clockA);
    TEST_ASSERT_TRUE(logA.begin());
    ConfigEngine engineA(cfgStoreA, logA);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engineA.begin(TEST_SCHEMA_A_V1, 0)));
    engineA.setNumber(TEST_INDEX_INT, 77, EventReason::Web, 0);
    engineA.setNumber(TEST_INDEX_FLOAT, 12.5f, EventReason::Web, 0);
    engineA.setNumber(TEST_INDEX_BOOL, 1, EventReason::Web, 0);
    engineA.setText(TEST_INDEX_TEXT, "roundtrip", EventReason::Web, 0);

    char buf[1024];
    size_t len = BackupCodec::exportJson(engineA, "9.9.9", buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    MemoryKvStore cfgStoreB, logStoreB;
    FakeClock clockB;
    EventLog logB(logStoreB, clockB);
    TEST_ASSERT_TRUE(logB.begin());
    ConfigEngine engineB(cfgStoreB, logB);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engineB.begin(TEST_SCHEMA_A_V1, 0)));

    uint32_t commitsBefore = static_cast<uint32_t>(cfgStoreB.commitCount);
    BackupImportResult result = BackupCodec::importJson(engineB, logB, buf, len, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_TRUE(result.applied > 0);
    TEST_ASSERT_TRUE(static_cast<uint32_t>(cfgStoreB.commitCount) > commitsBefore);  // committed without a tick

    TEST_ASSERT_EQUAL_FLOAT(77.0f, engineB.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_FLOAT(12.5f, engineB.getNumber(TEST_INDEX_FLOAT));
    TEST_ASSERT_TRUE(engineB.getBool(TEST_INDEX_BOOL));
    TEST_ASSERT_EQUAL_STRING("roundtrip", engineB.getText(TEST_INDEX_TEXT));

    const EventEntry* e = logB.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::BackupImported), e->type);
}

static void bak_test_wrong_type_rejected() {
    MemoryKvStore cfgStoreB, logStoreB;
    FakeClock clockB;
    EventLog logB(logStoreB, clockB);
    TEST_ASSERT_TRUE(logB.begin());
    ConfigEngine engineB(cfgStoreB, logB);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engineB.begin(TEST_SCHEMA_B_V1, 0)));
    char buf[1024];
    size_t len = BackupCodec::exportJson(engineB, "1.0.0", buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    MemoryKvStore cfgStoreA, logStoreA;
    FakeClock clockA;
    EventLog logA(logStoreA, clockA);
    TEST_ASSERT_TRUE(logA.begin());
    ConfigEngine engineA(cfgStoreA, logA);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engineA.begin(TEST_SCHEMA_A_V1, 0)));

    float before = engineA.getNumber(TEST_INDEX_INT);
    BackupImportResult result = BackupCodec::importJson(engineA, logA, buf, len, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::WrongType), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_FLOAT(before, engineA.getNumber(TEST_INDEX_INT));

    const EventEntry* e = logA.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::BackupRejected), e->type);
}

static void bak_test_missing_type_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* json = "{\"format\":1,\"configVersion\":1,\"settings\":{}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::MissingType), static_cast<int>(result.status));
}

static void bak_test_corrupt_json_variants_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* truncated = "{\"type\":\"test-a\",";
    BackupImportResult r1 = BackupCodec::importJson(engine, log, truncated, strlen(truncated), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Corrupt), static_cast<int>(r1.status));

    const char* rootArray = "[1,2,3]";
    BackupImportResult r2 = BackupCodec::importJson(engine, log, rootArray, strlen(rootArray), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Corrupt), static_cast<int>(r2.status));

    const char* empty = "";
    BackupImportResult r3 = BackupCodec::importJson(engine, log, empty, strlen(empty), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Corrupt), static_cast<int>(r3.status));
}

static void bak_test_unsupported_format_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* json = "{\"type\":\"test-a\",\"format\":2,\"configVersion\":1,\"settings\":{}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::UnsupportedFormat), static_cast<int>(result.status));
}

static void bak_test_invalid_value_rejects_whole_file_no_partial_apply() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    // testFloat is a valid key positioned before the bad testInt key; it must not
    // be applied once the file as a whole is refused (no partial apply).
    const char* json =
        "{\"type\":\"test-a\",\"format\":1,\"configVersion\":1,"
        "\"settings\":{\"testFloat\":33.5,\"testInt\":\"not-a-number\"}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::InvalidValue), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, engine.getNumber(TEST_INDEX_FLOAT));
    TEST_ASSERT_EQUAL_FLOAT(50.0f, engine.getNumber(TEST_INDEX_INT));
}

static void bak_test_out_of_range_value_clamped_on_import() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* json = "{\"type\":\"test-a\",\"format\":1,\"configVersion\":1,\"settings\":{\"testInt\":500}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT16(1, result.applied);
    TEST_ASSERT_EQUAL_UINT16(1, result.clamped);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, engine.getNumber(TEST_INDEX_INT));

    bool foundClamped = false;
    for (size_t i = 0; i < log.count(); ++i) {
        const EventEntry* e = log.newest(i);
        if (e->type == toU16(EventType::ConfigClamped)) {
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventReason::Import), e->reason);
            foundClamped = true;
        }
    }
    TEST_ASSERT_TRUE(foundClamped);
}

static void bak_test_missing_keys_keep_current_values() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
    engine.setNumber(TEST_INDEX_FLOAT, 44.0f, EventReason::Web, 0);

    const char* json = "{\"type\":\"test-a\",\"format\":1,\"configVersion\":1,\"settings\":{\"testInt\":10}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, engine.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_FLOAT(44.0f, engine.getNumber(TEST_INDEX_FLOAT));  // untouched, kept its current value
}

static void bak_test_unknown_key_ignored_and_counted() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* json =
        "{\"type\":\"test-a\",\"format\":1,\"configVersion\":1,"
        "\"settings\":{\"testInt\":10,\"doesNotExist\":123}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT16(1, result.applied);
    TEST_ASSERT_EQUAL_UINT16(1, result.ignoredUnknown);
}

static void bak_test_no_backup_key_ignored() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* json =
        "{\"type\":\"test-a\",\"format\":1,\"configVersion\":1,"
        "\"settings\":{\"wifiSsid\":\"someNetwork\"}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT16(0, result.applied);
    TEST_ASSERT_EQUAL_UINT16(1, result.ignoredUnknown);
    TEST_ASSERT_EQUAL_STRING("", engine.getText(commonIndex(CommonSetting::WifiSsid)));
}

static void bak_test_newer_config_version_applies_known_keys_and_logs() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    const char* json =
        "{\"type\":\"test-a\",\"format\":1,\"configVersion\":5,"
        "\"settings\":{\"testInt\":15,\"futureKey\":1}}";
    BackupImportResult result = BackupCodec::importJson(engine, log, json, strlen(json), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_TRUE(result.newerVersion);
    TEST_ASSERT_EQUAL_UINT16(5, result.backupVersion);
    TEST_ASSERT_EQUAL_UINT16(1, result.applied);
    TEST_ASSERT_EQUAL_UINT16(1, result.ignoredUnknown);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, engine.getNumber(TEST_INDEX_INT));

    bool foundNewer = false;
    for (size_t i = 0; i < log.count(); ++i) {
        const EventEntry* e = log.newest(i);
        if (e->type == toU16(EventType::BackupNewerVersion)) {
            TEST_ASSERT_EQUAL_FLOAT(5.0f, e->value);
            TEST_ASSERT_EQUAL_FLOAT(1.0f, e->aux);
            foundNewer = true;
        }
    }
    TEST_ASSERT_TRUE(foundNewer);
}

static void bak_test_older_backup_renames_key_through_migration() {
    MemoryKvStore cfgStoreV1, logStoreV1;
    FakeClock clockV1;
    EventLog logV1(logStoreV1, clockV1);
    TEST_ASSERT_TRUE(logV1.begin());
    ConfigEngine engineV1(cfgStoreV1, logV1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engineV1.begin(TEST_SCHEMA_A_V1, 0)));
    // "carried-value" is 13 chars, within oldTemp's 1..16 bound.
    ConfigStatus setSt = engineV1.setText(TEST_INDEX_TEXT, "carried-value", EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(setSt));

    char buf[1024];
    size_t len = BackupCodec::exportJson(engineV1, "1.0.0", buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    MemoryKvStore cfgStoreV2, logStoreV2;
    FakeClock clockV2;
    EventLog logV2(logStoreV2, clockV2);
    TEST_ASSERT_TRUE(logV2.begin());
    ConfigEngine engineV2(cfgStoreV2, logV2);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engineV2.begin(TEST_SCHEMA_A_V2, 0)));

    BackupImportResult result = BackupCodec::importJson(engineV2, logV2, buf, len, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_STRING("carried-value", engineV2.getText(TEST_INDEX_TEXT));  // now newTemp
}

static void bak_test_oversize_input_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    static char big[BACKUP_MAX_BYTES + 1];
    memset(big, 'a', sizeof(big));
    BackupImportResult result = BackupCodec::importJson(engine, log, big, sizeof(big), 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Corrupt), static_cast<int>(result.status));
}

// Runs every test in this suite. Call between UNITY_BEGIN()/UNITY_END() in the wrapper.
inline void runBackupSuite() {
    RUN_TEST(bak_test_export_contains_expected_fields_and_excludes_no_backup);
    RUN_TEST(bak_test_export_buffer_too_small_returns_zero);
    RUN_TEST(bak_test_round_trip_import_matches_export);
    RUN_TEST(bak_test_wrong_type_rejected);
    RUN_TEST(bak_test_missing_type_rejected);
    RUN_TEST(bak_test_corrupt_json_variants_rejected);
    RUN_TEST(bak_test_unsupported_format_rejected);
    RUN_TEST(bak_test_invalid_value_rejects_whole_file_no_partial_apply);
    RUN_TEST(bak_test_out_of_range_value_clamped_on_import);
    RUN_TEST(bak_test_missing_keys_keep_current_values);
    RUN_TEST(bak_test_unknown_key_ignored_and_counted);
    RUN_TEST(bak_test_no_backup_key_ignored);
    RUN_TEST(bak_test_newer_config_version_applies_known_keys_and_logs);
    RUN_TEST(bak_test_older_backup_renames_key_through_migration);
    RUN_TEST(bak_test_oversize_input_rejected);
}
