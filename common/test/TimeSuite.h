#pragma once
#include <unity.h>
#include <stdint.h>
#include "../lib/CoreEngine/src/CivilTime.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/Ds1307Codec.h"
#include "../lib/CoreEngine/src/TimeKeeper.h"
#include "../lib/CoreEngine/src/TzUtil.h"

// Native-safe Unity tests for CivilTime / Ds1307Codec / TzUtil / TimeKeeper
// (stage 02 phase 4): pure time logic, no hardware/RTC/network access.
// Header-only so both project test wrappers can include and run it via
// CommonSuite.h. Test names are prefixed time_ (D23).

// ---------------------------------------------------------------------------
// CivilTime
// ---------------------------------------------------------------------------

static void time_test_epoch_round_trip_1970_01_01() {
    CivilDateTime c{1970, 1, 1, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(isValidCivil(c));
    uint32_t epoch = civilToEpoch(c);
    TEST_ASSERT_EQUAL_UINT32(0, epoch);

    CivilDateTime back = epochToCivil(epoch);
    TEST_ASSERT_EQUAL_UINT16(1970, back.year);
    TEST_ASSERT_EQUAL_UINT8(1, back.month);
    TEST_ASSERT_EQUAL_UINT8(1, back.day);
    TEST_ASSERT_EQUAL_UINT8(0, back.hour);
    TEST_ASSERT_EQUAL_UINT8(0, back.minute);
    TEST_ASSERT_EQUAL_UINT8(0, back.second);
    TEST_ASSERT_EQUAL_UINT8(4, back.weekday);  // 1970-01-01 was a Thursday
}

static void time_test_epoch_round_trip_leap_day_2024() {
    CivilDateTime c{2024, 2, 29, 12, 34, 56, 0};
    TEST_ASSERT_TRUE(isValidCivil(c));
    uint32_t epoch = civilToEpoch(c);
    TEST_ASSERT_EQUAL_UINT32(1709210096u, epoch);

    CivilDateTime back = epochToCivil(epoch);
    TEST_ASSERT_EQUAL_UINT16(2024, back.year);
    TEST_ASSERT_EQUAL_UINT8(2, back.month);
    TEST_ASSERT_EQUAL_UINT8(29, back.day);
    TEST_ASSERT_EQUAL_UINT8(12, back.hour);
    TEST_ASSERT_EQUAL_UINT8(34, back.minute);
    TEST_ASSERT_EQUAL_UINT8(56, back.second);
    TEST_ASSERT_EQUAL_UINT8(4, back.weekday);  // 2024-02-29 was a Thursday
}

static void time_test_epoch_round_trip_2099_12_31() {
    CivilDateTime c{2099, 12, 31, 23, 59, 59, 0};
    TEST_ASSERT_TRUE(isValidCivil(c));
    uint32_t epoch = civilToEpoch(c);
    TEST_ASSERT_EQUAL_UINT32(4102444799u, epoch);

    CivilDateTime back = epochToCivil(epoch);
    TEST_ASSERT_EQUAL_UINT16(2099, back.year);
    TEST_ASSERT_EQUAL_UINT8(12, back.month);
    TEST_ASSERT_EQUAL_UINT8(31, back.day);
    TEST_ASSERT_EQUAL_UINT8(23, back.hour);
    TEST_ASSERT_EQUAL_UINT8(59, back.minute);
    TEST_ASSERT_EQUAL_UINT8(59, back.second);
    TEST_ASSERT_EQUAL_UINT8(4, back.weekday);  // 2099-12-31 was a Thursday
}

static void time_test_isValidCivil_rejects_feb_30() {
    CivilDateTime c{2024, 2, 30, 0, 0, 0, 0};  // 2024 is a leap year but Feb never has 30 days
    TEST_ASSERT_FALSE(isValidCivil(c));
}

static void time_test_isValidCivil_rejects_feb_29_non_leap_2025() {
    CivilDateTime c{2025, 2, 29, 0, 0, 0, 0};  // 2025 is not a leap year
    TEST_ASSERT_FALSE(isValidCivil(c));
}

// ---------------------------------------------------------------------------
// Ds1307Codec
// ---------------------------------------------------------------------------

static void time_test_ds1307_decode_known_register_set() {
    // 2024-06-15 14:25:36 UTC, weekday Saturday (reg3 = 6), 24h mode.
    const uint8_t regs[DS1307_REG_COUNT] = {0x36, 0x25, 0x14, 0x06, 0x15, 0x06, 0x24};
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RtcDecode::Ok), static_cast<int>(decodeDs1307(regs, utc)));
    TEST_ASSERT_EQUAL_UINT32(1718461536u, utc);
}

static void time_test_ds1307_decode_ch_bit_set_is_clock_halted() {
    // Same as the known register set above, but reg0 bit7 (CH) set.
    const uint8_t regs[DS1307_REG_COUNT] = {0xB6, 0x25, 0x14, 0x06, 0x15, 0x06, 0x24};
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RtcDecode::ClockHalted), static_cast<int>(decodeDs1307(regs, utc)));
}

static void time_test_ds1307_decode_bad_bcd() {
    // reg1 (minutes) = 0x1A: low nibble 0xA is not a valid BCD digit.
    const uint8_t regs[DS1307_REG_COUNT] = {0x36, 0x1A, 0x14, 0x06, 0x15, 0x06, 0x24};
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RtcDecode::BadBcd), static_cast<int>(decodeDs1307(regs, utc)));
}

static void time_test_ds1307_decode_implausible_year_2000() {
    // reg6 = 0x00 -> year 2000, below RTC_MIN_VALID_YEAR (2024).
    const uint8_t regs[DS1307_REG_COUNT] = {0x36, 0x25, 0x14, 0x06, 0x15, 0x06, 0x00};
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RtcDecode::ImplausibleYear), static_cast<int>(decodeDs1307(regs, utc)));
}

static void time_test_ds1307_decode_12h_mode_pm_noon() {
    // reg2 = 0x72: bit6 (12h mode) set, bit5 (PM) set, BCD hour 0x12 -> 12 PM (noon).
    const uint8_t regs[DS1307_REG_COUNT] = {0x00, 0x00, 0x72, 0x01, 0x01, 0x01, 0x24};
    uint32_t utc = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RtcDecode::Ok), static_cast<int>(decodeDs1307(regs, utc)));
    TEST_ASSERT_EQUAL_UINT32(1704110400u, utc);  // 2024-01-01 12:00:00 UTC
}

static void time_test_ds1307_encode_decode_round_trip() {
    const uint32_t utc = 2540535322u;  // 2050-07-04 08:15:22 UTC
    uint8_t regs[DS1307_REG_COUNT] = {0};
    encodeDs1307(utc, regs);
    TEST_ASSERT_EQUAL_UINT8(0, regs[0] & 0x80);  // CH clear
    TEST_ASSERT_EQUAL_UINT8(0, regs[2] & 0x40);  // 24h mode

    uint32_t decoded = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RtcDecode::Ok), static_cast<int>(decodeDs1307(regs, decoded)));
    TEST_ASSERT_EQUAL_UINT32(utc, decoded);
}

// ---------------------------------------------------------------------------
// TzUtil
// ---------------------------------------------------------------------------

static void time_test_tz_default_is_plausible() { TEST_ASSERT_TRUE(isPlausiblePosixTz(DEFAULT_TZ)); }
static void time_test_tz_utc0_is_plausible() { TEST_ASSERT_TRUE(isPlausiblePosixTz("UTC0")); }
static void time_test_tz_angle_bracket_name_is_plausible() { TEST_ASSERT_TRUE(isPlausiblePosixTz("<+03>-3")); }
static void time_test_tz_est5edt_with_rules_is_plausible() {
    TEST_ASSERT_TRUE(isPlausiblePosixTz("EST5EDT,M3.2.0,M11.1.0"));
}

static void time_test_tz_empty_is_not_plausible() { TEST_ASSERT_FALSE(isPlausiblePosixTz("")); }
static void time_test_tz_digits_only_is_not_plausible() { TEST_ASSERT_FALSE(isPlausiblePosixTz("12")); }
static void time_test_tz_two_letters_is_not_plausible() { TEST_ASSERT_FALSE(isPlausiblePosixTz("ab")); }
static void time_test_tz_too_long_is_not_plausible() {
    // 49 'A' characters: one over TZ_MAX_LEN (48).
    char tz[50];
    for (int i = 0; i < 49; ++i) tz[i] = 'A';
    tz[49] = '\0';
    TEST_ASSERT_FALSE(isPlausiblePosixTz(tz));
}
static void time_test_tz_null_is_not_plausible() { TEST_ASSERT_FALSE(isPlausiblePosixTz(nullptr)); }

static void time_test_effectiveTz_falls_back_to_default_on_implausible() {
    TEST_ASSERT_EQUAL_STRING(DEFAULT_TZ, effectiveTz("ab"));
    TEST_ASSERT_EQUAL_STRING(DEFAULT_TZ, effectiveTz(nullptr));
}

static void time_test_effectiveTz_keeps_plausible_configured() {
    TEST_ASSERT_EQUAL_STRING("UTC0", effectiveTz("UTC0"));
}

// ---------------------------------------------------------------------------
// TimeKeeper
// ---------------------------------------------------------------------------

static void time_test_keeper_invalid_reports_uptime_not_real() {
    TimeKeeper keeper;
    TEST_ASSERT_FALSE(keeper.isValid());
    TEST_ASSERT_TRUE(TimeSourceKind::None == keeper.source());

    Timestamp ts = keeper.now(12345);
    TEST_ASSERT_EQUAL_UINT32(12, ts.seconds);  // 12345 ms -> 12 s uptime
    TEST_ASSERT_FALSE(ts.realTime);
}

static void time_test_keeper_setFromRtc_valid_and_advances() {
    TimeKeeper keeper;
    keeper.setFromRtc(1718461536u, 1000);
    TEST_ASSERT_TRUE(keeper.isValid());
    TEST_ASSERT_TRUE(TimeSourceKind::Rtc == keeper.source());

    Timestamp ts = keeper.now(6000);  // 5000 ms later
    TEST_ASSERT_EQUAL_UINT32(1718461541u, ts.seconds);
    TEST_ASSERT_TRUE(ts.realTime);
}

static void time_test_keeper_setFromNtp_valid_source_and_lastSync() {
    TimeKeeper keeper;
    TEST_ASSERT_EQUAL_UINT32(0, keeper.lastNtpSyncUtc());

    keeper.setFromNtp(1718461536u, 2000);
    TEST_ASSERT_TRUE(keeper.isValid());
    TEST_ASSERT_TRUE(TimeSourceKind::Ntp == keeper.source());
    TEST_ASSERT_EQUAL_UINT32(1718461536u, keeper.lastNtpSyncUtc());

    Timestamp ts = keeper.now(2000);
    TEST_ASSERT_EQUAL_UINT32(1718461536u, ts.seconds);
    TEST_ASSERT_TRUE(ts.realTime);
}

static void time_test_keeper_ntpSyncDue_false_while_network_down() {
    TimeKeeper keeper;
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(0));
}

static void time_test_keeper_ntpSyncDue_true_right_after_network_up() {
    TimeKeeper keeper;
    keeper.onNetworkUp(0);
    TEST_ASSERT_TRUE(keeper.ntpSyncDue(0));
}

static void time_test_keeper_ntpSyncDue_retry_after_5min_while_unsynced() {
    TimeKeeper keeper;
    keeper.onNetworkUp(0);
    keeper.markNtpAttempt(0);
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(0));
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(TimeKeeper::RETRY_INTERVAL_MS - 1));
    TEST_ASSERT_TRUE(keeper.ntpSyncDue(TimeKeeper::RETRY_INTERVAL_MS));
}

static void time_test_keeper_ntpSyncDue_false_after_sync_until_24h_then_true() {
    TimeKeeper keeper;
    keeper.onNetworkUp(0);
    keeper.markNtpAttempt(0);
    keeper.setFromNtp(1718461536u, 1000);
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(1000));
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(1000 + TimeKeeper::RESYNC_INTERVAL_MS - 1));
    TEST_ASSERT_TRUE(keeper.ntpSyncDue(1000 + TimeKeeper::RESYNC_INTERVAL_MS));
}

static void time_test_keeper_reconnect_rearms_sync() {
    TimeKeeper keeper;
    keeper.onNetworkUp(0);
    keeper.markNtpAttempt(0);
    keeper.setFromNtp(1718461536u, 1000);
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(2000));

    keeper.onNetworkDown();
    TEST_ASSERT_FALSE(keeper.ntpSyncDue(2000));

    keeper.onNetworkUp(2000);
    TEST_ASSERT_TRUE(keeper.ntpSyncDue(2000));  // re-armed regardless of the 24h timer
}

// Runs every test in this suite. Call between UNITY_BEGIN()/UNITY_END() via
// CommonSuite.h's runCommonSuite().
inline void runTimeSuite() {
    RUN_TEST(time_test_epoch_round_trip_1970_01_01);
    RUN_TEST(time_test_epoch_round_trip_leap_day_2024);
    RUN_TEST(time_test_epoch_round_trip_2099_12_31);
    RUN_TEST(time_test_isValidCivil_rejects_feb_30);
    RUN_TEST(time_test_isValidCivil_rejects_feb_29_non_leap_2025);

    RUN_TEST(time_test_ds1307_decode_known_register_set);
    RUN_TEST(time_test_ds1307_decode_ch_bit_set_is_clock_halted);
    RUN_TEST(time_test_ds1307_decode_bad_bcd);
    RUN_TEST(time_test_ds1307_decode_implausible_year_2000);
    RUN_TEST(time_test_ds1307_decode_12h_mode_pm_noon);
    RUN_TEST(time_test_ds1307_encode_decode_round_trip);

    RUN_TEST(time_test_tz_default_is_plausible);
    RUN_TEST(time_test_tz_utc0_is_plausible);
    RUN_TEST(time_test_tz_angle_bracket_name_is_plausible);
    RUN_TEST(time_test_tz_est5edt_with_rules_is_plausible);
    RUN_TEST(time_test_tz_empty_is_not_plausible);
    RUN_TEST(time_test_tz_digits_only_is_not_plausible);
    RUN_TEST(time_test_tz_two_letters_is_not_plausible);
    RUN_TEST(time_test_tz_too_long_is_not_plausible);
    RUN_TEST(time_test_tz_null_is_not_plausible);
    RUN_TEST(time_test_effectiveTz_falls_back_to_default_on_implausible);
    RUN_TEST(time_test_effectiveTz_keeps_plausible_configured);

    RUN_TEST(time_test_keeper_invalid_reports_uptime_not_real);
    RUN_TEST(time_test_keeper_setFromRtc_valid_and_advances);
    RUN_TEST(time_test_keeper_setFromNtp_valid_source_and_lastSync);
    RUN_TEST(time_test_keeper_ntpSyncDue_false_while_network_down);
    RUN_TEST(time_test_keeper_ntpSyncDue_true_right_after_network_up);
    RUN_TEST(time_test_keeper_ntpSyncDue_retry_after_5min_while_unsynced);
    RUN_TEST(time_test_keeper_ntpSyncDue_false_after_sync_until_24h_then_true);
    RUN_TEST(time_test_keeper_reconnect_rearms_sync);
}
