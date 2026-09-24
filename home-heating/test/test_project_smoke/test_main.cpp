#include <unity.h>
#include <BoardConfig.h>
#include <ConfigEngine.h>
#include <EventLog.h>
#include "../../../common/test/fakes/FakeClock.h"
#include "../../../common/test/fakes/MemoryKvStore.h"
#include "../../src/HomeHeatingSchema.h"

void setUp() {}
void tearDown() {}

static void test_relay_channel_count_is_six() {
    TEST_ASSERT_EQUAL_UINT8(6, RELAY_CHANNEL_COUNT);
}

static void test_home_heating_schema_is_valid() {
    TEST_ASSERT_TRUE(ConfigEngine::validateSchema(HOME_HEATING_SCHEMA));
}

static void test_heating_enabled_persists_across_reboot() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());

    size_t heatingIdx = static_cast<size_t>(HomeHeatingSetting::HeatingEnabled);
    {
        ConfigEngine engine(cfgStore, log);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
            static_cast<int>(engine.begin(HOME_HEATING_SCHEMA, 0)));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(heatingIdx), engine.indexOf("heatingEnabled"));
        TEST_ASSERT_TRUE(engine.getBool(heatingIdx));  // default ON
        engine.setNumber(heatingIdx, 0, EventReason::Web, 0);  // false
        engine.flushNow();
    }

    ConfigEngine engine2(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(engine2.begin(HOME_HEATING_SCHEMA, 0)));
    TEST_ASSERT_FALSE(engine2.getBool(heatingIdx));
    // homeNoNeed is a boiler-room setting, not part of the home-heating schema.
    TEST_ASSERT_EQUAL_INT(-1, engine2.indexOf("homeNoNeed"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_relay_channel_count_is_six);
    RUN_TEST(test_home_heating_schema_is_valid);
    RUN_TEST(test_heating_enabled_persists_across_reboot);
    return UNITY_END();
}
