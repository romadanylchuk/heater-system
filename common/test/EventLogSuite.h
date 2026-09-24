#pragma once
#include <unity.h>
#include <stdint.h>
#include "../lib/CoreEngine/src/EventEntry.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "fakes/FakeClock.h"
#include "fakes/MemoryKvStore.h"

// Native-safe Unity tests for EventEntry / EventRateLimiter / EventLog (stage 02
// phase 1). Header-only so both project test wrappers can include and run it via
// CommonSuite.h. Test names are prefixed evlog_ (D23).

struct EvlogHookState {
    int calls = 0;
    EventEntry last{};
};

static void evlog_hook(const EventEntry& e, void* ctx) {
    EvlogHookState* state = static_cast<EvlogHookState*>(ctx);
    state->calls++;
    state->last = e;
}

static void evlog_test_crc16_known_vector() {
    const uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16Ccitt(data, 9));
}

static void evlog_test_slot_key_format() {
    char key[6];
    EventLog::slotKey(0, key);
    TEST_ASSERT_EQUAL_STRING("ev00", key);
    EventLog::slotKey(49, key);
    TEST_ASSERT_EQUAL_STRING("ev49", key);
}

static void evlog_test_empty_log() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    TEST_ASSERT_EQUAL_UINT32(0, log.count());
    TEST_ASSERT_EQUAL_UINT32(1, log.nextSeq());
    TEST_ASSERT_NULL(log.newest(0));
}

static void evlog_test_exactly_capacity_order_newest_first() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    for (uint16_t i = 0; i < EventLog::CAPACITY; ++i) {
        clock.advance(100);
        TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::DiagnosticWarning), i, static_cast<float>(i), 0, EventReason::Logic));
    }
    TEST_ASSERT_EQUAL_UINT32(EventLog::CAPACITY, log.count());
    const EventEntry* newest = log.newest(0);
    TEST_ASSERT_NOT_NULL(newest);
    TEST_ASSERT_EQUAL_UINT32(EventLog::CAPACITY, newest->seq);
    const EventEntry* oldest = log.newest(EventLog::CAPACITY - 1);
    TEST_ASSERT_NOT_NULL(oldest);
    TEST_ASSERT_EQUAL_UINT32(1, oldest->seq);
}

static void evlog_test_wrap_around_keeps_newest_50() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    const uint32_t total = 55;
    for (uint32_t i = 0; i < total; ++i) {
        clock.advance(100);
        TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::DiagnosticWarning), static_cast<uint16_t>(i), 0, 0, EventReason::Logic));
    }
    TEST_ASSERT_EQUAL_UINT32(EventLog::CAPACITY, log.count());
    TEST_ASSERT_EQUAL_UINT32(total + 1, log.nextSeq());
    TEST_ASSERT_EQUAL_UINT32(total, log.newest(0)->seq);
    TEST_ASSERT_EQUAL_UINT32(total - EventLog::CAPACITY + 1, log.newest(EventLog::CAPACITY - 1)->seq);
}

static void evlog_test_persistence_reload() {
    MemoryKvStore store;
    FakeClock clock;
    {
        EventLog log(store, clock);
        TEST_ASSERT_TRUE(log.begin());
        for (int i = 0; i < 5; ++i) {
            clock.advance(100);
            TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), static_cast<uint16_t>(i), 0, 0, EventReason::Boot));
        }
    }
    EventLog log2(store, clock);
    TEST_ASSERT_TRUE(log2.begin());
    TEST_ASSERT_EQUAL_UINT32(5, log2.count());
    TEST_ASSERT_EQUAL_UINT32(6, log2.nextSeq());
    TEST_ASSERT_EQUAL_UINT32(5, log2.newest(0)->seq);
    TEST_ASSERT_EQUAL_UINT32(1, log2.newest(4)->seq);
}

static void evlog_test_corrupt_slot_skipped() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    for (int i = 0; i < 5; ++i) {
        clock.advance(100);
        TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), static_cast<uint16_t>(i), 0, 0, EventReason::Boot));
    }
    // seq 3 lives in slot 3; overwrite it with a structurally valid but CRC-broken entry.
    char key[6];
    EventLog::slotKey(3, key);
    EventEntry corrupt{};
    corrupt.seq = 3;
    corrupt.timestamp = 0;
    corrupt.type = toU16(EventType::Reboot);
    corrupt.source = 2;
    corrupt.value = 0;
    corrupt.aux = 0;
    corrupt.reason = static_cast<uint8_t>(EventReason::Boot);
    corrupt.flags = 0;
    corrupt.crc = 0xDEAD;  // wrong crc
    store.rawSetBlob(key, &corrupt, sizeof(EventEntry));

    EventLog log2(store, clock);
    TEST_ASSERT_TRUE(log2.begin());
    TEST_ASSERT_EQUAL_UINT32(4, log2.count());
    TEST_ASSERT_EQUAL_UINT32(5, log2.newest(0)->seq);
    TEST_ASSERT_EQUAL_UINT32(4, log2.newest(1)->seq);
    TEST_ASSERT_EQUAL_UINT32(2, log2.newest(2)->seq);
    TEST_ASSERT_EQUAL_UINT32(1, log2.newest(3)->seq);
}

static void evlog_test_wrong_size_blob_skipped() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    for (int i = 0; i < 3; ++i) {
        clock.advance(100);
        TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), static_cast<uint16_t>(i), 0, 0, EventReason::Boot));
    }
    // seq 2 lives in slot 2; overwrite with a wrong-size blob.
    char key[6];
    EventLog::slotKey(2, key);
    uint8_t junk[10] = {0};
    store.rawSetBlob(key, junk, sizeof(junk));

    EventLog log2(store, clock);
    TEST_ASSERT_TRUE(log2.begin());
    TEST_ASSERT_EQUAL_UINT32(2, log2.count());
    TEST_ASSERT_EQUAL_UINT32(3, log2.newest(0)->seq);
    TEST_ASSERT_EQUAL_UINT32(1, log2.newest(1)->seq);
}

static void evlog_test_persist_failed_keeps_ram_and_fires_hook() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    store.failWrites = true;
    EvlogHookState state;
    log.setPublishHook(evlog_hook, &state);

    bool accepted = log.logEvent(toU16(EventType::NvsError), EVENT_SOURCE_NVS, 1, 2, EventReason::Boot);
    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventLogStatus::PersistFailed), static_cast<int>(log.lastStatus()));
    TEST_ASSERT_FALSE(log.lastPersistOk());
    TEST_ASSERT_EQUAL_UINT32(1, log.count());
    TEST_ASSERT_EQUAL_INT(1, state.calls);
    TEST_ASSERT_EQUAL_UINT32(1, state.last.seq);
}

static void evlog_test_rate_limit_suppresses_within_window() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());

    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    clock.advance(59999);
    TEST_ASSERT_FALSE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventLogStatus::RateLimited), static_cast<int>(log.lastStatus()));
    TEST_ASSERT_EQUAL_UINT32(1, log.count());

    clock.advance(1);  // 60000ms since the first acceptance
    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_UINT32(2, log.count());
}

static void evlog_test_rate_limit_different_key_not_suppressed() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());

    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    clock.advance(10);
    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_WATCHDOG, 0, 0, EventReason::Boot));
    clock.advance(10);
    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::NvsError), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_UINT32(3, log.count());
}

static void evlog_test_real_time_flag_and_rate_limit_with_uptime() {
    MemoryKvStore store;
    FakeClock clock;
    clock.realTime = false;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());

    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_UINT8(0, log.newest(0)->flags & EVENT_FLAG_REAL_TIME);

    clock.advance(30000);
    TEST_ASSERT_FALSE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));

    clock.realTime = true;
    clock.utc = 1234;
    clock.advance(30001);  // total 60001ms since the first acceptance
    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    const EventEntry* e = log.newest(0);
    TEST_ASSERT_EQUAL_UINT8(EVENT_FLAG_REAL_TIME, e->flags & EVENT_FLAG_REAL_TIME);
    TEST_ASSERT_EQUAL_UINT32(clock.utc, e->timestamp);
}

static void evlog_test_hook_fires_only_for_accepted() {
    MemoryKvStore store;
    FakeClock clock;
    EventLog log(store, clock);
    TEST_ASSERT_TRUE(log.begin());
    EvlogHookState state;
    log.setPublishHook(evlog_hook, &state);

    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_INT(1, state.calls);

    clock.advance(1000);
    TEST_ASSERT_FALSE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_INT(1, state.calls);

    clock.advance(60000);
    TEST_ASSERT_TRUE(log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot));
    TEST_ASSERT_EQUAL_INT(2, state.calls);
}

// Runs every test in this suite. Call between UNITY_BEGIN()/UNITY_END() in the wrapper.
inline void runEventLogSuite() {
    RUN_TEST(evlog_test_crc16_known_vector);
    RUN_TEST(evlog_test_slot_key_format);
    RUN_TEST(evlog_test_empty_log);
    RUN_TEST(evlog_test_exactly_capacity_order_newest_first);
    RUN_TEST(evlog_test_wrap_around_keeps_newest_50);
    RUN_TEST(evlog_test_persistence_reload);
    RUN_TEST(evlog_test_corrupt_slot_skipped);
    RUN_TEST(evlog_test_wrong_size_blob_skipped);
    RUN_TEST(evlog_test_persist_failed_keeps_ram_and_fires_hook);
    RUN_TEST(evlog_test_rate_limit_suppresses_within_window);
    RUN_TEST(evlog_test_rate_limit_different_key_not_suppressed);
    RUN_TEST(evlog_test_real_time_flag_and_rate_limit_with_uptime);
    RUN_TEST(evlog_test_hook_fires_only_for_accepted);
}
