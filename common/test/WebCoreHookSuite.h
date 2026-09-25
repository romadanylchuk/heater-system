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
#include "fakes/FakeClock.h"
#include "fakes/InMemoryCommandQueue.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/TestSchema.h"

// Native tests for the stage-05 CoreRuntime command-result hook (D7): the hook
// fires once per applied command on the loop task, after lastCommandId/Status
// were updated and after the payload was freed. Test names are prefixed
// web_hook_.

namespace {

struct HookRecord {
    static constexpr size_t MAX = 8;
    const CommonState* state = nullptr;
    size_t count = 0;
    uint32_t ids[MAX] = {};
    CommandStatus statuses[MAX] = {};
    CommandType types[MAX] = {};
    bool payloadNull[MAX] = {};
    bool lastCmdUpdated[MAX] = {};
};

void recordHook(const Command& cmd, CommandStatus status, void* ctx) {
    HookRecord* r = static_cast<HookRecord*>(ctx);
    if (r->count >= HookRecord::MAX) {
        return;
    }
    size_t i = r->count++;
    r->ids[i] = cmd.id;
    r->statuses[i] = status;
    r->types[i] = cmd.type;
    r->payloadNull[i] = cmd.payload == nullptr;
    r->lastCmdUpdated[i] = r->state != nullptr && r->state->system.lastCommandId == cmd.id &&
                           r->state->system.lastCommandStatus == static_cast<uint8_t>(status);
}

}  // namespace

static void web_hook_test_reports_set_commands() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);
    HookRecord rec;
    rec.state = &state;
    runtime.setResultHook(recordHook, &rec);

    TEST_ASSERT_TRUE(queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), 70, EventReason::Web, 101)));
    TEST_ASSERT_TRUE(queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), 500, EventReason::Web, 102)));
    TEST_ASSERT_TRUE(
        queue.post(makeSetText(static_cast<uint16_t>(TEST_INDEX_TEXT), "hooked", EventReason::Web, 103)));
    Command none{};
    none.type = CommandType::None;
    none.id = 104;
    TEST_ASSERT_TRUE(queue.post(none));
    runtime.tick(0);

    TEST_ASSERT_EQUAL_UINT32(4, rec.count);
    TEST_ASSERT_EQUAL_UINT32(101, rec.ids[0]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(rec.statuses[0]));
    TEST_ASSERT_EQUAL_UINT32(102, rec.ids[1]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Clamped), static_cast<int>(rec.statuses[1]));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, config.getNumber(TEST_INDEX_INT));
    TEST_ASSERT_EQUAL_UINT32(103, rec.ids[2]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(rec.statuses[2]));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::SetText), static_cast<int>(rec.types[2]));
    TEST_ASSERT_EQUAL_UINT32(104, rec.ids[3]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::InvalidCommand), static_cast<int>(rec.statuses[3]));
    for (size_t i = 0; i < rec.count; ++i) {
        TEST_ASSERT_TRUE(rec.lastCmdUpdated[i]);
        TEST_ASSERT_TRUE(rec.payloadNull[i]);
    }
}

static void web_hook_test_import_backup_payload_freed_before_hook() {
    MemoryKvStore cfgStoreSrc, logStoreSrc;
    FakeClock clockSrc;
    EventLog logSrc(logStoreSrc, clockSrc);
    TEST_ASSERT_TRUE(logSrc.begin());
    ConfigEngine configSrc(cfgStoreSrc, logSrc);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(configSrc.begin(TEST_SCHEMA_A_V1, 0)));
    configSrc.setNumber(TEST_INDEX_INT, 33, EventReason::Web, 0);
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
    HookRecord rec;
    rec.state = &state;
    runtime.setResultHook(recordHook, &rec);

    char* good = static_cast<char*>(malloc(len + 1));
    memcpy(good, exportBuf, len + 1);
    TEST_ASSERT_TRUE(queue.post(makeImportBackup(good, len, EventReason::Web, 201)));
    const char junk[] = "{not json";
    char* bad = static_cast<char*>(malloc(sizeof(junk)));
    memcpy(bad, junk, sizeof(junk));
    TEST_ASSERT_TRUE(queue.post(makeImportBackup(bad, sizeof(junk) - 1, EventReason::Web, 202)));
    runtime.tick(0);

    TEST_ASSERT_EQUAL_UINT32(2, rec.count);
    TEST_ASSERT_EQUAL_UINT32(201, rec.ids[0]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Ok), static_cast<int>(rec.statuses[0]));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::ImportBackup), static_cast<int>(rec.types[0]));
    TEST_ASSERT_TRUE(rec.payloadNull[0]);
    TEST_ASSERT_EQUAL_UINT32(202, rec.ids[1]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::ImportFailed), static_cast<int>(rec.statuses[1]));
    TEST_ASSERT_TRUE(rec.payloadNull[1]);
    TEST_ASSERT_EQUAL_FLOAT(33.0f, config.getNumber(TEST_INDEX_INT));
}

static void web_hook_test_no_hook_and_cleared_hook() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(TEST_SCHEMA_A_V1, 0)));
    InMemoryCommandQueue queue;
    CommonState state{};
    CoreRuntime runtime(state, config, log, queue);

    // No hook registered: applying commands must work unchanged.
    TEST_ASSERT_TRUE(queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), 60, EventReason::Web, 301)));
    runtime.tick(0);
    TEST_ASSERT_EQUAL_UINT32(301, state.system.lastCommandId);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, config.getNumber(TEST_INDEX_INT));

    // Registered then cleared: the hook is no longer called.
    HookRecord rec;
    rec.state = &state;
    runtime.setResultHook(recordHook, &rec);
    runtime.setResultHook(nullptr, nullptr);
    TEST_ASSERT_TRUE(queue.post(makeSetNumber(static_cast<uint16_t>(TEST_INDEX_INT), 61, EventReason::Web, 302)));
    runtime.tick(0);
    TEST_ASSERT_EQUAL_UINT32(0, rec.count);
    TEST_ASSERT_EQUAL_UINT32(302, state.system.lastCommandId);
}

inline void runWebCoreHookSuite() {
    RUN_TEST(web_hook_test_reports_set_commands);
    RUN_TEST(web_hook_test_import_backup_payload_freed_before_hook);
    RUN_TEST(web_hook_test_no_hook_and_cleared_hook);
}
