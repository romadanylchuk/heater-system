#pragma once
#include <unity.h>
#include <cmath>
#include <optional>
#include <stdint.h>
#include <string.h>
#include "../lib/HwEngine/src/Ds18b20Codec.h"
#include "../lib/HwEngine/src/SensorAddress.h"
#include "../lib/HwEngine/src/SensorDebounce.h"
#include "fakes/FakeOneWireBus.h"

// Native-safe Unity tests for the DS18B20 codec (CRC8/ROM validation/
// scratchpad decode), the address text codec and the debounce state machine
// (stage 03 phase 3, D14/D15). Header-only, run via HwSuite.h. Test names
// are prefixed sens_ (D24).

static void sens_test_crc8_known_vectors() {
    uint8_t rom[8];
    FakeOneWireBus::makeRom(0x12345678, rom);
    TEST_ASSERT_EQUAL_UINT8(rom[7], Ds18b20::crc8(rom, 7));

    // Maxim/Dallas application-note CRC8 example.
    static const uint8_t maximExample[8] = {0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xA2};
    TEST_ASSERT_EQUAL_UINT8(0xA2, Ds18b20::crc8(maximExample, 7));
}

static void sens_test_is_valid_rom() {
    uint8_t rom[8];
    FakeOneWireBus::makeRom(1, rom);
    TEST_ASSERT_TRUE(Ds18b20::isValidRom(rom));

    uint8_t badCrc[8];
    memcpy(badCrc, rom, 8);
    badCrc[7] ^= 0xFF;
    TEST_ASSERT_FALSE(Ds18b20::isValidRom(badCrc));

    uint8_t wrongFamily[8];
    memcpy(wrongFamily, rom, 8);
    wrongFamily[0] = 0x10;
    wrongFamily[7] = Ds18b20::crc8(wrongFamily, 7);
    TEST_ASSERT_FALSE(Ds18b20::isValidRom(wrongFamily));
}

namespace {
void buildScratch(float tempC, uint8_t out[9]) {
    long raw = lroundf(tempC * 16.0f);
    out[0] = static_cast<uint8_t>(raw & 0xFF);
    out[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    for (size_t i = 2; i < 8; ++i) out[i] = 0;
    out[8] = Ds18b20::crc8(out, 8);
}
}  // namespace

static void sens_test_decode_scratchpad_good_values() {
    uint8_t scratch[9];

    buildScratch(25.0625f, scratch);
    SensorReading r = Ds18b20::decodeScratchpad(true, scratch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::Ok), static_cast<int>(r.status));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0625f, r.tempC);

    buildScratch(-10.5f, scratch);
    r = Ds18b20::decodeScratchpad(true, scratch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::Ok), static_cast<int>(r.status));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -10.5f, r.tempC);

    buildScratch(85.0f, scratch);
    r = Ds18b20::decodeScratchpad(true, scratch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::Ok), static_cast<int>(r.status));  // codec-level only
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 85.0f, r.tempC);
}

static void sens_test_decode_scratchpad_bad_values() {
    uint8_t allFF[9];
    memset(allFF, 0xFF, 9);
    SensorReading r = Ds18b20::decodeScratchpad(true, allFF);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::NoResponse), static_cast<int>(r.status));

    uint8_t scratch[9];
    buildScratch(25.0f, scratch);
    r = Ds18b20::decodeScratchpad(false, scratch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::NoResponse), static_cast<int>(r.status));

    uint8_t allZero[9] = {};
    r = Ds18b20::decodeScratchpad(true, allZero);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::CrcError), static_cast<int>(r.status));

    buildScratch(25.0f, scratch);
    scratch[8] ^= 0xFF;  // corrupt CRC
    r = Ds18b20::decodeScratchpad(true, scratch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::CrcError), static_cast<int>(r.status));

    buildScratch(130.0f, scratch);
    r = Ds18b20::decodeScratchpad(true, scratch);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ReadStatus::OutOfRange), static_cast<int>(r.status));
}

static void sens_test_format_and_parse_address_roundtrip() {
    uint8_t rom[8];
    FakeOneWireBus::makeRom(0xABCDEF01, rom);
    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(rom, text);
    TEST_ASSERT_EQUAL_INT(16, static_cast<int>(strlen(text)));

    uint8_t parsed[8];
    TEST_ASSERT_TRUE(parseAddress(text, parsed));
    TEST_ASSERT_TRUE(addressEquals(rom, parsed));
}

static void sens_test_parse_address_lowercase_accepted() {
    uint8_t parsed[8];
    TEST_ASSERT_TRUE(parseAddress("28ff641e8716033c", parsed));
    uint8_t expected[8] = {0x28, 0xFF, 0x64, 0x1E, 0x87, 0x16, 0x03, 0x3C};
    TEST_ASSERT_TRUE(addressEquals(expected, parsed));
}

static void sens_test_parse_address_rejects_bad_input() {
    uint8_t parsed[8];
    TEST_ASSERT_FALSE(parseAddress("28FF641E8716033", parsed));    // 15 chars
    TEST_ASSERT_TRUE(addressIsZero(parsed));
    TEST_ASSERT_FALSE(parseAddress("28FF641E8716033CA", parsed));  // 17 chars
    TEST_ASSERT_FALSE(parseAddress("28FF641E8716033G", parsed));   // non-hex
    TEST_ASSERT_FALSE(parseAddress("", parsed));
    TEST_ASSERT_TRUE(addressIsZero(parsed));
}

static void sens_debounce_three_bad_faults_on_third() {
    SensorDebounce d;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unknown), static_cast<int>(d.state()));
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::NoResponse, 0.0f}) == SensorDebounce::Transition::None);
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::NoResponse, 0.0f}) == SensorDebounce::Transition::None);
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::NoResponse, 0.0f}) == SensorDebounce::Transition::Faulted);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(d.state()));
}

static void sens_debounce_unknown_to_ok_after_three_good_no_events_before() {
    SensorDebounce d;
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::Ok, 20.0f}) == SensorDebounce::Transition::None);
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::Ok, 20.5f}) == SensorDebounce::Transition::None);
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::Ok, 21.0f}) == SensorDebounce::Transition::BecameOk);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(d.state()));
    TEST_ASSERT_TRUE(d.value().has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.0f, *d.value());
}

static void sens_debounce_fault_to_ok_after_three_good_is_recovered() {
    SensorDebounce d;
    for (int i = 0; i < 3; ++i) d.onReading(SensorReading{ReadStatus::NoResponse, 0.0f});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(d.state()));

    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::Ok, 22.0f}) == SensorDebounce::Transition::None);
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::Ok, 22.0f}) == SensorDebounce::Transition::None);
    TEST_ASSERT_TRUE(d.onReading(SensorReading{ReadStatus::Ok, 22.0f}) == SensorDebounce::Transition::Recovered);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(d.state()));
}

static void sens_debounce_value_empty_unless_ok_last_good_held() {
    SensorDebounce d;
    TEST_ASSERT_FALSE(d.value().has_value());  // Unknown
    d.onReading(SensorReading{ReadStatus::Ok, 30.0f});
    d.onReading(SensorReading{ReadStatus::Ok, 30.0f});
    d.onReading(SensorReading{ReadStatus::Ok, 30.0f});  // -> Ok
    TEST_ASSERT_TRUE(d.value().has_value());

    // 1-2 bad samples while Ok keep the last good value.
    d.onReading(SensorReading{ReadStatus::CrcError, 0.0f});
    TEST_ASSERT_TRUE(d.value().has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, *d.value());
    d.onReading(SensorReading{ReadStatus::CrcError, 0.0f});
    TEST_ASSERT_TRUE(d.value().has_value());

    d.onReading(SensorReading{ReadStatus::CrcError, 0.0f});  // 3rd bad -> Fault
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(d.state()));
    TEST_ASSERT_FALSE(d.value().has_value());
}

static void sens_debounce_power_on_rule() {
    // 85.0 first-after-reset -> bad.
    {
        SensorDebounce d;
        SensorDebounce::Transition t = d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        TEST_ASSERT_TRUE(t == SensorDebounce::Transition::None);
        TEST_ASSERT_FALSE(d.lastSampleGood());
    }
    // 85.0 after NoResponse -> bad.
    {
        SensorDebounce d;
        d.onReading(SensorReading{ReadStatus::Ok, 40.0f});
        d.onReading(SensorReading{ReadStatus::NoResponse, 0.0f});
        d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        TEST_ASSERT_FALSE(d.lastSampleGood());
    }
    // 84.9 then 85.0 -> valid.
    {
        SensorDebounce d;
        d.onReading(SensorReading{ReadStatus::Ok, 84.9f});
        d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        TEST_ASSERT_TRUE(d.lastSampleGood());
    }
    // 40.0 then 85.0 -> bad (jump > 5).
    {
        SensorDebounce d;
        d.onReading(SensorReading{ReadStatus::Ok, 40.0f});
        d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        TEST_ASSERT_FALSE(d.lastSampleGood());
    }
    // 85.0 x3 at boot -> Fault, never Ok.
    {
        SensorDebounce d;
        d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        SensorDebounce::Transition t = d.onReading(SensorReading{ReadStatus::Ok, 85.0f});
        TEST_ASSERT_TRUE(t == SensorDebounce::Transition::Faulted);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(d.state()));
    }
}

// Runs every test in this suite. Call from runHwSuite().
inline void runSensorSuite() {
    RUN_TEST(sens_test_crc8_known_vectors);
    RUN_TEST(sens_test_is_valid_rom);
    RUN_TEST(sens_test_decode_scratchpad_good_values);
    RUN_TEST(sens_test_decode_scratchpad_bad_values);
    RUN_TEST(sens_test_format_and_parse_address_roundtrip);
    RUN_TEST(sens_test_parse_address_lowercase_accepted);
    RUN_TEST(sens_test_parse_address_rejects_bad_input);
    RUN_TEST(sens_debounce_three_bad_faults_on_third);
    RUN_TEST(sens_debounce_unknown_to_ok_after_three_good_no_events_before);
    RUN_TEST(sens_debounce_fault_to_ok_after_three_good_is_recovered);
    RUN_TEST(sens_debounce_value_empty_unless_ok_last_good_held);
    RUN_TEST(sens_debounce_power_on_rule);
}
