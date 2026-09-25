#pragma once
#include <unity.h>
#include <RelayMask.h>
#include "BackupSuite.h"
#include "ConfigEngineSuite.h"
#include "EventLogSuite.h"
#include "HwSuite.h"
#include "NetSuite.h"
#include "RuntimeSuite.h"
#include "TimeSuite.h"
#include "WebSuite.h"

// Shared native-safe Unity tests for RelayMask and CoreEngine. Header-only so both
// project test wrappers (test/test_common/test_main.cpp) can include and run it.

static_assert(relayAllOffByte(true) == 0xFF, "active-low all-off byte must be 0xFF");

static void test_relay_all_off_active_low() {
    TEST_ASSERT_EQUAL_UINT8(0xFF, relayAllOffByte(true));
}

static void test_relay_all_off_active_high() {
    TEST_ASSERT_EQUAL_UINT8(0x00, relayAllOffByte(false));
}

static void test_relay_all_off_constant_matches_config() {
    TEST_ASSERT_EQUAL_UINT8(relayAllOffByte(RELAY_ACTIVE_LOW), RELAY_ALL_OFF_BYTE);
}

// Runs every test in this suite. Call between UNITY_BEGIN()/UNITY_END() in the wrapper.
inline void runCommonSuite() {
    RUN_TEST(test_relay_all_off_active_low);
    RUN_TEST(test_relay_all_off_active_high);
    RUN_TEST(test_relay_all_off_constant_matches_config);
    runEventLogSuite();
    runConfigEngineSuite();
    runBackupSuite();
    runRuntimeSuite();
    runTimeSuite();
    runHwSuite();
    runNetSuite();
    runWebSuite();
}
