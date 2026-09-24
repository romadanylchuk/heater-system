#pragma once
#include <unity.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/CoreEngine/src/BackupCodec.h"
#include "../lib/CoreEngine/src/Command.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/CoreRuntime.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/FactoryResetGate.h"
#include "fakes/FakeClock.h"
#include "fakes/InMemoryCommandQueue.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/TestSchema.h"

// Native-safe Unity tests for CoreRuntime / InMemoryCommandQueue / FactoryResetGate
// (stage 02 phase 3): command draining/application, the debounce as seen through
// the runtime, ImportBackup payload ownership, factory-reset orchestration, and
// the pure DI1 gate state machine. Header-only so both project test wrappers can
// include and run it via CommonSuite.h. Test names are prefixed rt_ (D23).

static void rt_test_set_number_applied_on_tick() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    TEST_ASSERT_TRUE(queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), 88, EventReason::Web, 42)));
    runtime.tick(0);

    TEST_ASSERT_EQUAL_FLOAT(88.0f, config.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_UINT32(42, state.system.lastCommandId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::Ok), state.system.lastCommandStatus);

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::ConfigChanged), e->type);
    TEST_ASSERT_TRUE(state.eventLog == &log);  // CoreRuntime's constructor wires this (D21)
    TEST_ASSERT_EQUAL_UINT32(log.count(), state.eventLog->count());
}

static void rt_test_set_text_applied() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    TEST_ASSERT_TRUE(
        queue.post(makeSetText(static_cast<uint16_t>(TEST_INDEX_TEXT), "viaqueue", EventReason::Web, 7)));
    runtime.tick(0);

    TEST_ASSERT_EQUAL_STRING("viaqueue", config.getText(TEST_INDEX_TEXT));
    TEST_ASSERT_EQUAL_UINT32(7, state.system.lastCommandId);
}

static void rt_test_invalid_index_rejected() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    TEST_ASSERT_TRUE(queue.post(makeSetNumber(9999, 1, EventReason::Web, 1)));
    runtime.tick(0);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::InvalidCommand), state.system.lastCommandStatus);
}

static void rt_test_queue_full_rejects_17th_post() {
    InMemoryCommandQueue queue;
    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_TRUE(
            queue.post(makeSetNumber(0, static_cast<float>(i), EventReason::Web, static_cast<uint32_t>(i))));
    }
    TEST_ASSERT_FALSE(queue.post(makeSetNumber(0, 99, EventReason::Web, 99)));
}

// Test-only CommandQueue fake with capacity > MAX_COMMANDS_PER_TICK, used solely to
// exercise CoreRuntime::tick's per-tick drain cap (rt_test_tick_drains_at_most_16_per_tick
// below). InMemoryCommandQueue intentionally stays at DEPTH == 16 to match
// FreeRtosCommandQueue::DEPTH for production parity, so it can never hold more
// commands than the cap and cannot distinguish a capped drain from an uncapped one.
class OverflowCommandQueue : public CommandQueue {
public:
    static constexpr size_t DEPTH = 32;

    bool post(const Command& cmd) override {
        if (_count >= DEPTH) {
            return false;
        }
        _items[(_head + _count) % DEPTH] = cmd;
        ++_count;
        return true;
    }

    bool tryReceive(Command& out) override {
        if (_count == 0) {
            return false;
        }
        out = _items[_head];
        _head = (_head + 1) % DEPTH;
        --_count;
        return true;
    }

    size_t size() const { return _count; }

private:
    Command _items[DEPTH];
    size_t _head = 0;
    size_t _count = 0;
};

static void rt_test_tick_drains_at_most_16_per_tick() {
    // Uses OverflowCommandQueue (capacity 32, test-only, defined above) instead of
    // InMemoryCommandQueue (capacity 16) so that more commands can be pending than
    // CoreRuntime::MAX_COMMANDS_PER_TICK permits per tick(). This is what actually
    // exercises the cap: 20 commands posted, one tick() must apply exactly 16 and
    // leave 4 behind, and a second tick() must drain the remainder.
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    OverflowCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    TEST_ASSERT_EQUAL_UINT32(16, static_cast<uint32_t>(CoreRuntime::MAX_COMMANDS_PER_TICK));

    for (uint32_t i = 0; i < 20; ++i) {
        TEST_ASSERT_TRUE(
            queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), static_cast<float>(i), EventReason::Web, i)));
    }
    TEST_ASSERT_EQUAL_UINT32(20, static_cast<uint32_t>(queue.size()));

    runtime.tick(0);
    TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(queue.size()));   // 20 - 16 remain queued
    TEST_ASSERT_EQUAL_UINT32(15, state.system.lastCommandId);           // only ids 0..15 applied this tick

    runtime.tick(0);
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(queue.size()));   // remainder drained on the next tick
    TEST_ASSERT_EQUAL_UINT32(19, state.system.lastCommandId);           // ids 16..19 applied
}

static void rt_test_debounce_through_runtime_commits_after_2s() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    TEST_ASSERT_TRUE(queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), 66, EventReason::Web, 1)));
    runtime.tick(1000);
    TEST_ASSERT_TRUE(config.isDirty());
    uint32_t commitsBefore = static_cast<uint32_t>(cfgStore.commitCount);

    runtime.tick(2900);  // 1900ms since the change: still under the 2000ms debounce
    TEST_ASSERT_EQUAL_UINT32(commitsBefore, static_cast<uint32_t>(cfgStore.commitCount));
    TEST_ASSERT_TRUE(config.isDirty());

    runtime.tick(3000);  // 2000ms since the change
    TEST_ASSERT_EQUAL_UINT32(commitsBefore + 1, static_cast<uint32_t>(cfgStore.commitCount));
    TEST_ASSERT_FALSE(config.isDirty());
}

static void rt_test_import_backup_command_frees_payload() {
    MemoryKvStore cfgStoreSrc, logStoreSrc;
    FakeClock clockSrc;
    EventLog logSrc(logStoreSrc, clockSrc);
    TEST_ASSERT_TRUE(logSrc.begin());
    ConfigEngine configSrc(cfgStoreSrc, logSrc);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(configSrc.begin(TEST_SCHEMA_A_V1, 0)));
    configSrc.setNumber(TEST_INDEX_INT, 55, EventReason::Web, 0);
    char exportBuf[1024];
    size_t len = BackupCodec::exportJson(configSrc, "1.0.0", exportBuf, sizeof(exportBuf));
    TEST_ASSERT_TRUE(len > 0);

    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    char* payload = static_cast<char*>(malloc(len + 1));
    memcpy(payload, exportBuf, len + 1);
    TEST_ASSERT_TRUE(queue.post(makeImportBackup(payload, len, EventReason::Web, 5)));

    runtime.tick(0);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, config.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::Ok), state.system.lastCommandStatus);

    // A second apply of an already-freed (payload == nullptr) import must not crash.
    Command reImport = makeImportBackup(nullptr, 0, EventReason::Web, 6);
    CommandStatus st = runtime.apply(reImport, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::ImportFailed), static_cast<int>(st));
    TEST_ASSERT_NULL(reImport.payload);
}

static void rt_test_web_factory_reset_command_erases_cfg_keeps_log_requests_reboot() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    config.setText(commonIndex(CommonSetting::WebUser), "custom", EventReason::Web, 0);
    config.flushNow();
    log.logEvent(toU16(EventType::Reboot), EVENT_SOURCE_SYSTEM, 0, 0, EventReason::Boot);

    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    uint32_t logCountBefore = log.count();

    TEST_ASSERT_TRUE(queue.post(makeFactoryReset(EventReason::Web, 10)));
    runtime.tick(5000);

    TEST_ASSERT_EQUAL_STRING("admin", config.getText(commonIndex(CommonSetting::WebUser)));
    TEST_ASSERT_TRUE(state.system.setupApRequested);
    TEST_ASSERT_TRUE(state.system.rebootRequested);
    TEST_ASSERT_TRUE(state.system.rebootAtMs == 5000ull + CoreRuntime::REBOOT_DELAY_MS);

    TEST_ASSERT_EQUAL_UINT32(logCountBefore + 1, log.count());  // + the FactoryReset entry
    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::FactoryReset), e->type);
    TEST_ASSERT_EQUAL_UINT16(EVENT_SOURCE_WEB, e->source);
}

static void rt_test_di1_factory_reset_does_not_request_reboot() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    runtime.performFactoryReset(EventReason::Di1, 1234);

    TEST_ASSERT_TRUE(state.system.setupApRequested);
    TEST_ASSERT_FALSE(state.system.rebootRequested);

    const EventEntry* e = log.newest(0);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::FactoryReset), e->type);
    TEST_ASSERT_EQUAL_UINT16(EVENT_SOURCE_DI1, e->source);
}

static void rt_test_gate_inactive_when_not_closed_at_power_on() {
    FactoryResetGate gate;
    gate.begin(false, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Inactive), static_cast<int>(gate.phase()));
}

static void rt_test_gate_confirms_after_hold() {
    FactoryResetGate gate;
    gate.begin(true, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Countdown), static_cast<int>(gate.phase()));
    ResetGatePhase phase = gate.update(true, FactoryResetGate::HOLD_MS);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Confirmed), static_cast<int>(phase));
}

static void rt_test_gate_aborts_on_sustained_release() {
    FactoryResetGate gate;
    gate.begin(true, 0);
    gate.update(true, 4000);
    ResetGatePhase phase = gate.update(false, 4000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Countdown), static_cast<int>(phase));  // openSince just armed
    phase = gate.update(false, 4000 + FactoryResetGate::RELEASE_DEBOUNCE_MS);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Aborted), static_cast<int>(phase));
}

static void rt_test_gate_short_bounce_does_not_abort() {
    FactoryResetGate gate;
    gate.begin(true, 0);
    gate.update(false, 4000);                        // opens
    ResetGatePhase phase = gate.update(true, 4020);   // closes again after 20ms < 50ms debounce
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Countdown), static_cast<int>(phase));
    phase = gate.update(true, FactoryResetGate::HOLD_MS);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResetGatePhase::Confirmed), static_cast<int>(phase));
}

static void rt_test_gate_seconds_left_counts_down() {
    FactoryResetGate gate;
    gate.begin(true, 0);
    TEST_ASSERT_EQUAL_UINT8(10, gate.secondsLeft(0));
    TEST_ASSERT_EQUAL_UINT8(1, gate.secondsLeft(9001));
    TEST_ASSERT_EQUAL_UINT8(1, gate.secondsLeft(9999));
    TEST_ASSERT_EQUAL_UINT8(0, gate.secondsLeft(10000));
}

// Runs every test in this suite. Call between UNITY_BEGIN()/UNITY_END() in the wrapper.
inline void runRuntimeSuite() {
    RUN_TEST(rt_test_set_number_applied_on_tick);
    RUN_TEST(rt_test_set_text_applied);
    RUN_TEST(rt_test_invalid_index_rejected);
    RUN_TEST(rt_test_queue_full_rejects_17th_post);
    RUN_TEST(rt_test_tick_drains_at_most_16_per_tick);
    RUN_TEST(rt_test_debounce_through_runtime_commits_after_2s);
    RUN_TEST(rt_test_import_backup_command_frees_payload);
    RUN_TEST(rt_test_web_factory_reset_command_erases_cfg_keeps_log_requests_reboot);
    RUN_TEST(rt_test_di1_factory_reset_does_not_request_reboot);
    RUN_TEST(rt_test_gate_inactive_when_not_closed_at_power_on);
    RUN_TEST(rt_test_gate_confirms_after_hold);
    RUN_TEST(rt_test_gate_aborts_on_sustained_release);
    RUN_TEST(rt_test_gate_short_bounce_does_not_abort);
    RUN_TEST(rt_test_gate_seconds_left_counts_down);
}
