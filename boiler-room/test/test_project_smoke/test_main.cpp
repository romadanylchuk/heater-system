#include <unity.h>
#include <BoardConfig.h>
#include <ConfigEngine.h>
#include <EventLog.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../src/BoilerRoomSchema.h"

void setUp() {}
void tearDown() {}

static void test_relay_channel_count_is_six() {
    TEST_ASSERT_EQUAL_UINT8(6, RELAY_CHANNEL_COUNT);
}

static void test_boiler_room_schema_is_valid() {
    TEST_ASSERT_TRUE(ConfigEngine::validateSchema(BOILER_ROOM_SCHEMA));
}

static void test_ha_flags_persist_across_reboot() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());

    size_t noNeedIdx = static_cast<size_t>(BoilerRoomSetting::HomeNoNeed);
    {
        ConfigEngine engine(cfgStore, log);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(engine.begin(BOILER_ROOM_SCHEMA, 0)));
        engine.setNumber(noNeedIdx, 1, EventReason::Web, 0);   // true
        engine.flushNow();
    }

    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(BOILER_ROOM_SCHEMA, 0)));
    TEST_ASSERT_TRUE(engine2.getBool(noNeedIdx));
    // heatingEnabled is a home-heating setting, not part of the boiler-room schema.
    TEST_ASSERT_EQUAL_INT(-1, engine2.indexOf("heatingEnabled"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_relay_channel_count_is_six);
    RUN_TEST(test_boiler_room_schema_is_valid);
    RUN_TEST(test_ha_flags_persist_across_reboot);
    return UNITY_END();
}
