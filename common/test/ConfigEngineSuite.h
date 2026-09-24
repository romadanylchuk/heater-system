#pragma once
#include <unity.h>
#include <math.h>
#include <string.h>
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/KvStore.h"
#include "../lib/CoreEngine/src/SettingClamp.h"
#include "../lib/CoreEngine/src/SettingDescriptor.h"
#include "fakes/FakeClock.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/TestSchema.h"

// Native-safe Unity tests for ConfigEngine (stage 02 phase 2): defaults, clamping,
// change/reject/unchanged, debounce, migration/downgrade, store failures, factory
// reset, and validateSchema. Header-only so both project test wrappers can include
// and run it via CommonSuite.h. Test names are prefixed cfg_ (D23).
//
// Each test wires a ConfigEngine over its own MemoryKvStore ("cfg") plus a separate
// EventLog over its own MemoryKvStore ("log"), matching how the log namespace is
// independent of the config namespace in firmware (D5).

static void cfg_test_fresh_store_defaults() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);

    ConfigStatus st = engine.begin(TEST_SCHEMA_A_V1, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_TRUE(engine.isReady());
    TEST_ASSERT_EQUAL_UINT16(0, engine.storedVersionAtBoot());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, engine.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, engine.getNumber(TEST_INDEX_FLOAT));
    TEST_ASSERT_FALSE(engine.getBool(TEST_INDEX_BOOL));
    TEST_ASSERT_EQUAL_STRING("hi", engine.getText(TEST_INDEX_TEXT));
    TEST_ASSERT_EQUAL_STRING("admin", engine.getText(commonIndex(CommonSetting::WebUser)));

    int32_t verVal = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StoreStatus::Ok), static_cast<int>(cfgStore.getI32("cfgVer", verVal)));
    TEST_ASSERT_EQUAL_INT32(1, verVal);
}

static void cfg_test_missing_key_uses_default() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    cfgStore.setI32("cfgVer", 1);
    cfgStore.commit();

    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, engine.getNumber(TEST_INDEX_FLOAT));
}

static void cfg_test_set_in_range_logs_changed() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setNumber(TEST_INDEX_INT, 75, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(75.0f, engine.getNumber(TEST_INDEX_INT));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigChanged), e->type);
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(EVENT_SOURCE_SETTING_BASE + TEST_INDEX_INT), e->source);
    TEST_ASSERT_EQUAL_FLOAT(75.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, e->aux);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventReason::Web), e->reason);
}

static void cfg_test_set_above_max_clamped() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setNumber(TEST_INDEX_INT, 999, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Clamped), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, engine.getNumber(TEST_INDEX_INT));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigClamped), e->type);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(999.0f, e->aux);
}

static void cfg_test_set_below_min_clamped() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setNumber(TEST_INDEX_FLOAT, -50.0f, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Clamped), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(-10.5f, engine.getNumber(TEST_INDEX_FLOAT));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigClamped), e->type);
    TEST_ASSERT_EQUAL_FLOAT(-10.5f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(-50.0f, e->aux);
}

static void cfg_test_set_nan_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setNumber(TEST_INDEX_FLOAT, NAN, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Rejected), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, engine.getNumber(TEST_INDEX_FLOAT));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigRejected), e->type);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e->value);
}

static void cfg_test_set_infinity_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setNumber(TEST_INDEX_INT, INFINITY, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Rejected), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(50.0f, engine.getNumber(TEST_INDEX_INT));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigRejected), e->type);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e->value);
}

static void cfg_test_int_rounds_and_bool_coerces() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setNumber(TEST_INDEX_INT, 42.6f, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));  // rounding alone != Clamped
    TEST_ASSERT_EQUAL_FLOAT(43.0f, engine.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_INT32(43, engine.getInt(TEST_INDEX_INT));

    engine.setNumber(TEST_INDEX_BOOL, 5.0f, EventReason::Web, 0);
    TEST_ASSERT_TRUE(engine.getBool(TEST_INDEX_BOOL));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, engine.getNumber(TEST_INDEX_BOOL));
}

static void cfg_test_int_huge_values_clamp_to_limits() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    // Beyond `long` range: must clamp to max, not be rounded to garbage/0 first.
    ConfigStatus st = engine.setNumber(TEST_INDEX_INT, 1e10f, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Clamped), static_cast<int>(st));
    TEST_ASSERT_EQUAL_INT32(100, engine.getInt(TEST_INDEX_INT));
    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigClamped), e->type);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(1e10f, e->aux);

    clock.advance(61000);  // step past the 60 s per type+source event rate limit
    st = engine.setNumber(TEST_INDEX_INT, -1e10f, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Clamped), static_cast<int>(st));
    TEST_ASSERT_EQUAL_INT32(0, engine.getInt(TEST_INDEX_INT));
    e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigClamped), e->type);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(-1e10f, e->aux);

    // Just past the limit but rounding back onto it is still InRange (unchanged semantics).
    const SettingDescriptor* d = engine.descriptor(TEST_INDEX_INT);
    TEST_ASSERT_NOT_NULL(d);
    ClampResult r = clampNumber(*d, 100.3f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ClampOutcome::InRange), static_cast<int>(r.outcome));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, r.value);
}

static void cfg_test_set_same_value_unchanged() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    uint32_t countBefore = log.count();
    uint32_t writesBefore = static_cast<uint32_t>(cfgStore.writeCount);

    ConfigStatus st = engine.setNumber(TEST_INDEX_INT, 50.0f, EventReason::Web, 0);  // 50 is the default
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Unchanged), static_cast<int>(st));
    TEST_ASSERT_EQUAL_UINT32(countBefore, log.count());
    TEST_ASSERT_FALSE(engine.isDirty());

    engine.flushNow();
    TEST_ASSERT_EQUAL_UINT32(writesBefore, static_cast<uint32_t>(cfgStore.writeCount));
}

static void cfg_test_text_length_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus tooShort = engine.setText(TEST_INDEX_TEXT, "", EventReason::Web, 0);  // min length 1
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Rejected), static_cast<int>(tooShort));

    char longStr[20];
    memset(longStr, 'a', sizeof(longStr) - 1);
    longStr[sizeof(longStr) - 1] = '\0';  // length 19 > max 16
    ConfigStatus tooLong = engine.setText(TEST_INDEX_TEXT, longStr, EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Rejected), static_cast<int>(tooLong));

    TEST_ASSERT_EQUAL_STRING("hi", engine.getText(TEST_INDEX_TEXT));
}

static void cfg_test_secret_change_logs_zero() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    ConfigStatus st = engine.setText(TEST_INDEX_SECRET, "newsecret", EventReason::Web, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_STRING("newsecret", engine.getText(TEST_INDEX_SECRET));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigChanged), e->type);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e->aux);
}

static void cfg_test_debounce_batches_writes() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    engine.setNumber(TEST_INDEX_INT, 10, EventReason::Web, 0);
    engine.setNumber(TEST_INDEX_FLOAT, 30.5f, EventReason::Web, 500);
    engine.setNumber(TEST_INDEX_BOOL, 1, EventReason::Web, 1500);

    uint32_t commitsBefore = static_cast<uint32_t>(cfgStore.commitCount);
    engine.tick(3400);
    TEST_ASSERT_EQUAL_UINT32(commitsBefore, static_cast<uint32_t>(cfgStore.commitCount));
    TEST_ASSERT_TRUE(engine.isDirty());

    engine.tick(3500);
    TEST_ASSERT_EQUAL_UINT32(commitsBefore + 1, static_cast<uint32_t>(cfgStore.commitCount));
    TEST_ASSERT_FALSE(engine.isDirty());

    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(TEST_SCHEMA_A_V1, 0)));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, engine2.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_FLOAT(30.5f, engine2.getNumber(TEST_INDEX_FLOAT));
    TEST_ASSERT_TRUE(engine2.getBool(TEST_INDEX_BOOL));
}

static void cfg_test_flush_now_commits_immediately() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    engine.setNumber(TEST_INDEX_INT, 77, EventReason::Web, 0);
    uint32_t commitsBefore = static_cast<uint32_t>(cfgStore.commitCount);
    ConfigStatus st = engine.flushNow();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_TRUE(static_cast<uint32_t>(cfgStore.commitCount) > commitsBefore);
    TEST_ASSERT_FALSE(engine.isDirty());
}

static void cfg_test_reload_returns_persisted_values() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    {
        ConfigEngine engine(cfgStore, log);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
        engine.setText(TEST_INDEX_TEXT, "reload", EventReason::Web, 0);
        engine.flushNow();
    }
    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(TEST_SCHEMA_A_V1, 0)));
    TEST_ASSERT_EQUAL_STRING("reload", engine2.getText(TEST_INDEX_TEXT));
}

static void cfg_test_boot_clamps_out_of_range_stored_value() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    cfgStore.setI32("cfgVer", 1);
    cfgStore.setI32("testInt", 500);  // above max 100
    cfgStore.commit();

    ConfigEngine engine(cfgStore, log);
    ConfigStatus st = engine.begin(TEST_SCHEMA_A_V1, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, engine.getNumber(TEST_INDEX_INT));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigClamped), e->type);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventReason::Boot), e->reason);

    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(TEST_SCHEMA_A_V1, 0)));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, engine2.getNumber(TEST_INDEX_INT));  // written back
}

static void cfg_test_migration_renames_key() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    {
        ConfigEngine engineV1(cfgStore, log);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(engineV1.begin(TEST_SCHEMA_A_V1, 0)));
        engineV1.setText(TEST_INDEX_TEXT, "carried", EventReason::Web, 0);
        engineV1.flushNow();
    }

    ConfigEngine engineV2(cfgStore, log);
    ConfigStatus st = engineV2.begin(TEST_SCHEMA_A_V2, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_UINT16(1, engineV2.storedVersionAtBoot());
    TEST_ASSERT_EQUAL_STRING("carried", engineV2.getText(TEST_INDEX_TEXT));  // now newTemp

    int32_t verVal = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StoreStatus::Ok), static_cast<int>(cfgStore.getI32("cfgVer", verVal)));
    TEST_ASSERT_EQUAL_INT32(2, verVal);

    char buf[32];
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StoreStatus::NotFound),
        static_cast<int>(cfgStore.getStr("oldTmp", buf, sizeof(buf))));

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigMigrated), e->type);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, e->aux);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventReason::Migration), e->reason);
}

static void cfg_test_migration_does_not_overwrite_existing_new_key() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    cfgStore.setI32("cfgVer", 1);
    cfgStore.setStr("oldTmp", "old-value");
    cfgStore.setStr("newTmp", "already-set");
    cfgStore.commit();

    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V2, 0)));
    TEST_ASSERT_EQUAL_STRING("already-set", engine.getText(TEST_INDEX_TEXT));
}

static void cfg_test_downgrade_keeps_known_keys_and_logs() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    cfgStore.setI32("cfgVer", 5);
    cfgStore.setI32("testInt", 66);
    cfgStore.commit();

    ConfigEngine engine(cfgStore, log);
    ConfigStatus st = engine.begin(TEST_SCHEMA_A_V1, 0);  // schema is v1, store says v5
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_FLOAT(66.0f, engine.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_UINT16(5, engine.storedVersionAtBoot());

    int32_t verVal = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StoreStatus::Ok), static_cast<int>(cfgStore.getI32("cfgVer", verVal)));
    TEST_ASSERT_EQUAL_INT32(5, verVal);  // not rewritten

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigDowngrade), e->type);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, e->value);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, e->aux);
}

static void cfg_test_missing_cfgver_with_keys_runs_migrations_from_zero() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    cfgStore.setStr("oldTmp", "legacy");  // some key present, cfgVer absent
    cfgStore.commit();

    ConfigEngine engine(cfgStore, log);
    ConfigStatus st = engine.begin(TEST_SCHEMA_A_V2, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_UINT16(0, engine.storedVersionAtBoot());
    TEST_ASSERT_EQUAL_STRING("legacy", engine.getText(TEST_INDEX_TEXT));

    int32_t verVal = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StoreStatus::Ok), static_cast<int>(cfgStore.getI32("cfgVer", verVal)));
    TEST_ASSERT_EQUAL_INT32(2, verVal);
}

static void cfg_test_flush_failure_keeps_ram_and_dirty() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    engine.setNumber(TEST_INDEX_INT, 80, EventReason::Web, 0);
    cfgStore.failWrites = true;
    ConfigStatus st = engine.flushNow();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::StoreError), static_cast<int>(st));
    TEST_ASSERT_FALSE(engine.lastStoreOk());
    TEST_ASSERT_TRUE(engine.isDirty());
    TEST_ASSERT_EQUAL_FLOAT(80.0f, engine.getNumber(TEST_INDEX_INT));
}

static void cfg_test_begin_store_error_ready_on_defaults() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    cfgStore.failOpen = true;

    uint32_t logCountBefore = log.count();

    ConfigEngine engine(cfgStore, log);
    ConfigStatus st = engine.begin(TEST_SCHEMA_A_V1, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::StoreError), static_cast<int>(st));
    TEST_ASSERT_TRUE(engine.isReady());
    TEST_ASSERT_FALSE(engine.lastStoreOk());
    TEST_ASSERT_EQUAL_FLOAT(50.0f, engine.getNumber(TEST_INDEX_INT));

    // A store-wide OpenFailed must yield exactly one NvsError event, not a
    // ConfigRejected per setting (the per-setting reads all fail silently and
    // fall back to defaults; only the single end-of-function NvsError covers it).
    TEST_ASSERT_EQUAL_UINT32(logCountBefore + 1, log.count());
    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(EventType::NvsError), e->type);
}

static void cfg_test_factory_reset_erases_cfg_keeps_log_store() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));
    engine.setText(commonIndex(CommonSetting::WebUser), "custom", EventReason::Web, 0);
    engine.flushNow();
    log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot);

    uint32_t logCountBefore = log.count();
    uint32_t logStoreEraseBefore = static_cast<uint32_t>(logStore.eraseCount);

    ConfigStatus st = engine.factoryReset();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(st));
    TEST_ASSERT_EQUAL_STRING("admin", engine.getText(commonIndex(CommonSetting::WebUser)));
    TEST_ASSERT_EQUAL_STRING("admin", engine.getText(commonIndex(CommonSetting::WebPass)));
    TEST_ASSERT_FALSE(engine.isDirty());

    int32_t verVal = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StoreStatus::Ok), static_cast<int>(cfgStore.getI32("cfgVer", verVal)));
    TEST_ASSERT_EQUAL_INT32(1, verVal);

    TEST_ASSERT_EQUAL_UINT32(logCountBefore, log.count());
    TEST_ASSERT_EQUAL_UINT32(logStoreEraseBefore, static_cast<uint32_t>(logStore.eraseCount));
    TEST_ASSERT_TRUE(cfgStore.eraseCount > 0);
}

static void cfg_test_validate_schema_rejects_long_nvskey() {
    static const SettingDescriptor bad[] = {
        intSetting("longKeyName", "thisNvsKeyIsWayTooLong", "Bad", "Bad2", nullptr, "test", 0, 10, 0),
    };
    static const SettingsTable badTable = makeTable(bad);
    static const SettingsTable tables[] = {COMMON_SETTINGS_TABLE, badTable};
    ConfigSchema schema = {"test-bad", 1, tables, 2, nullptr, 0};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_validate_schema_rejects_duplicate_key() {
    static const SettingDescriptor dup[] = {
        intSetting("dupKey", "dupA", "A", "A2", nullptr, "test", 0, 10, 0),
        intSetting("dupKey", "dupB", "B", "B2", nullptr, "test", 0, 10, 0),
    };
    static const SettingsTable dupTable = makeTable(dup);
    static const SettingsTable tables[] = {COMMON_SETTINGS_TABLE, dupTable};
    ConfigSchema schema = {"test-bad", 1, tables, 2, nullptr, 0};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_validate_schema_rejects_duplicate_nvskey() {
    static const SettingDescriptor dup[] = {
        intSetting("keyA", "dupNvs", "A", "A2", nullptr, "test", 0, 10, 0),
        intSetting("keyB", "dupNvs", "B", "B2", nullptr, "test", 0, 10, 0),
    };
    static const SettingsTable dupTable = makeTable(dup);
    static const SettingsTable tables[] = {COMMON_SETTINGS_TABLE, dupTable};
    ConfigSchema schema = {"test-bad", 1, tables, 2, nullptr, 0};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_validate_schema_rejects_min_greater_than_default() {
    static const SettingDescriptor bad[] = {
        intSetting("badRange", "badRange", "Bad", "Bad2", nullptr, "test", 10, 20, 5),  // default 5 < min 10
    };
    static const SettingsTable badTable = makeTable(bad);
    static const SettingsTable tables[] = {COMMON_SETTINGS_TABLE, badTable};
    ConfigSchema schema = {"test-bad", 1, tables, 2, nullptr, 0};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_validate_schema_rejects_missing_common_table() {
    static const SettingDescriptor only[] = {
        intSetting("soloKey", "soloKey", "Solo", "Solo2", nullptr, "test", 0, 10, 0),
    };
    static const SettingsTable soloTable = makeTable(only);
    static const SettingsTable tables[] = {soloTable};
    ConfigSchema schema = {"test-bad", 1, tables, 1, nullptr, 0};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_validate_schema_rejects_nvskey_equals_cfgver() {
    static const SettingDescriptor bad[] = {
        intSetting("cfgVerKey", "cfgVer", "Bad", "Bad2", nullptr, "test", 0, 10, 0),
    };
    static const SettingsTable badTable = makeTable(bad);
    static const SettingsTable tables[] = {COMMON_SETTINGS_TABLE, badTable};
    ConfigSchema schema = {"test-bad", 1, tables, 2, nullptr, 0};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_validate_schema_rejects_rename_to_unknown_key() {
    static const KeyRename badRenames[] = {
        {"someOld", "doesNotExist", "someOldNvs", "noSuchNvs"},
    };
    static const ConfigMigration badMigrations[] = {
        {2, badRenames, 1},
    };
    static const SettingsTable tables[] = {COMMON_SETTINGS_TABLE};
    ConfigSchema schema = {"test-bad", 2, tables, 1, badMigrations, 1};
    TEST_ASSERT_FALSE(ConfigEngine::validateSchema(schema));
}

static void cfg_test_common_settings_nvskeys_fit_limit() {
    for (size_t i = 0; i < COMMON_SETTING_COUNT; ++i) {
        TEST_ASSERT_TRUE(strlen(COMMON_SETTINGS[i].nvsKey) <= NVS_KEY_MAX_LEN);
    }
}

static void cfg_test_indexof_and_descriptor_bounds() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine engine(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine.begin(TEST_SCHEMA_A_V1, 0)));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(TEST_INDEX_INT), engine.indexOf("testInt"));
    TEST_ASSERT_EQUAL_INT(-1, engine.indexOf("doesNotExist"));
    TEST_ASSERT_NOT_NULL(engine.descriptor(TEST_INDEX_INT));
    TEST_ASSERT_NULL(engine.descriptor(engine.count()));
    TEST_ASSERT_NULL(engine.descriptor(9999));
}

// Runs every test in this suite. Call between UNITY_BEGIN()/UNITY_END() in the wrapper.
inline void runConfigEngineSuite() {
    RUN_TEST(cfg_test_fresh_store_defaults);
    RUN_TEST(cfg_test_missing_key_uses_default);
    RUN_TEST(cfg_test_set_in_range_logs_changed);
    RUN_TEST(cfg_test_set_above_max_clamped);
    RUN_TEST(cfg_test_set_below_min_clamped);
    RUN_TEST(cfg_test_set_nan_rejected);
    RUN_TEST(cfg_test_set_infinity_rejected);
    RUN_TEST(cfg_test_int_rounds_and_bool_coerces);
    RUN_TEST(cfg_test_int_huge_values_clamp_to_limits);
    RUN_TEST(cfg_test_set_same_value_unchanged);
    RUN_TEST(cfg_test_text_length_rejected);
    RUN_TEST(cfg_test_secret_change_logs_zero);
    RUN_TEST(cfg_test_debounce_batches_writes);
    RUN_TEST(cfg_test_flush_now_commits_immediately);
    RUN_TEST(cfg_test_reload_returns_persisted_values);
    RUN_TEST(cfg_test_boot_clamps_out_of_range_stored_value);
    RUN_TEST(cfg_test_migration_renames_key);
    RUN_TEST(cfg_test_migration_does_not_overwrite_existing_new_key);
    RUN_TEST(cfg_test_downgrade_keeps_known_keys_and_logs);
    RUN_TEST(cfg_test_missing_cfgver_with_keys_runs_migrations_from_zero);
    RUN_TEST(cfg_test_flush_failure_keeps_ram_and_dirty);
    RUN_TEST(cfg_test_begin_store_error_ready_on_defaults);
    RUN_TEST(cfg_test_factory_reset_erases_cfg_keeps_log_store);
    RUN_TEST(cfg_test_validate_schema_rejects_long_nvskey);
    RUN_TEST(cfg_test_validate_schema_rejects_duplicate_key);
    RUN_TEST(cfg_test_validate_schema_rejects_duplicate_nvskey);
    RUN_TEST(cfg_test_validate_schema_rejects_min_greater_than_default);
    RUN_TEST(cfg_test_validate_schema_rejects_missing_common_table);
    RUN_TEST(cfg_test_validate_schema_rejects_nvskey_equals_cfgver);
    RUN_TEST(cfg_test_validate_schema_rejects_rename_to_unknown_key);
    RUN_TEST(cfg_test_common_settings_nvskeys_fit_limit);
    RUN_TEST(cfg_test_indexof_and_descriptor_bounds);
}
