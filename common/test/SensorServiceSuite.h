#pragma once
#include <unity.h>
#include <optional>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/BackupCodec.h"
#include "../lib/CoreEngine/src/Command.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/ConfigSchema.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/HardwareStatus.h"
#include "../lib/CoreEngine/src/SettingDescriptor.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/HwSettings.h"
#include "../lib/HwEngine/src/SensorAddress.h"
#include "../lib/HwEngine/src/SensorService.h"
#include "fakes/FakeOneWireBus.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for SensorService (stage 03 phase 3, D16-D18): the
// bus cycle (D17), boot/rescan missing-sensor evaluation (D18), mapping
// sync/validation/duplicate-clearing (D16), assign/clear commands and the
// backup round-trip. Header-only, run via HwSuite.h. Test names are
// prefixed senssvc_ (D24).

namespace {

inline constexpr SettingDescriptor SENSSVC_TEST_SETTINGS[] = {
    textSetting("sensorA", "sensA", "Sensor A", "Датчик A", "sensors", 0, 16, ""),
    textSetting("sensorB", "sensB", "Sensor B", "Датчик B", "sensors", 0, 16, ""),
    textSetting("sensorC", "sensC", "Sensor C", "Датчик C", "sensors", 0, 16, ""),
};
inline constexpr SettingsTable SENSSVC_TEST_SETTINGS_TABLE = makeTable(SENSSVC_TEST_SETTINGS);
inline constexpr SettingsTable SENSSVC_TEST_TABLES[] = {COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE,
    SENSSVC_TEST_SETTINGS_TABLE};
inline constexpr ConfigSchema SENSSVC_TEST_SCHEMA = {"test-senssvc", 1, SENSSVC_TEST_TABLES, 3, nullptr, 0};

constexpr size_t SENS_IDX_A = HW_SETTINGS_END + 0;
constexpr size_t SENS_IDX_B = HW_SETTINGS_END + 1;
constexpr size_t SENS_IDX_C = HW_SETTINGS_END + 2;

inline constexpr LogicalSensorDesc SENSSVC_TEST_SENSORS[] = {
    {"A", "sensorA"},
    {"B", "sensorB"},
    {"C", "sensorC"},
};

// Drives `steps` 1 s tick() calls starting at `now`; returns the next nowMs.
uint64_t senssvc_driveTicks(SensorService& svc, uint64_t now, int steps) {
    for (int i = 0; i < steps; ++i) {
        svc.tick(now);
        now += 1000;
    }
    return now;
}

uint32_t senssvc_missingBit(uint8_t logical) {
    return 1u << (SENSOR_MISSING_ALARM_BIT_BASE + logical);
}

}  // namespace

static void senssvc_test_cycle_timing_request_then_read() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    bus.addDevice(romA);

    SensorService svc(bus, config, events);
    TEST_ASSERT_TRUE(svc.begin(SENSSVC_TEST_SENSORS, 3, 0));

    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(bus.requestCount));
    svc.tick(0);  // tick 0: request
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(bus.requestCount));
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(bus.readCount));

    svc.tick(1000);  // tick 1: read (conversion elapsed)
    TEST_ASSERT_TRUE(bus.readCount > 0);
    size_t firstReadCount = bus.readCount;

    svc.tick(2000);  // tick 2: request again (2000 ms since the last request)
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(bus.requestCount));
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(firstReadCount), static_cast<uint32_t>(bus.readCount));

    svc.tick(2500);  // still within CONVERSION_MS: no read yet
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(firstReadCount), static_cast<uint32_t>(bus.readCount));

    svc.tick(3000);  // tick 3: read again
    TEST_ASSERT_TRUE(bus.readCount > firstReadCount);
}

static void senssvc_test_assigned_sensor_ok_after_three_reads_no_fault_at_boot() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    bus.addDevice(romA);
    bus.setTemp(romA, 21.5f);

    SensorService svc(bus, config, events);
    TEST_ASSERT_TRUE(svc.begin(SENSSVC_TEST_SENSORS, 3, 0));
    TEST_ASSERT_TRUE(svc.assign(0, romA, EventReason::Web, 0) == CommandStatus::Ok);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unknown), static_cast<int>(svc.state(0)));
    senssvc_driveTicks(svc, 0, 6);  // requests at 0/2000/4000, reads at 1000/3000/5000

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(svc.state(0)));
    TEST_ASSERT_TRUE(svc.value(0).has_value());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, *svc.value(0));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::SensorFault)));
}

static void senssvc_test_unplug_faults_and_alarms_replug_recovers() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    bus.addDevice(romA);
    bus.setTemp(romA, 20.0f);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc.assign(0, romA, EventReason::Web, 0);
    uint64_t now = senssvc_driveTicks(svc, 0, 6);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(svc.state(0)));
    TEST_ASSERT_FALSE(svc.missing(0));

    bus.setAbsent(romA);
    now = senssvc_driveTicks(svc, now, 6);  // 3 more request/read pairs -> 3 bad reads

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(svc.state(0)));
    TEST_ASSERT_TRUE(svc.missing(0));
    TEST_ASSERT_FALSE(svc.value(0).has_value());
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::SensorFault, EVENT_SOURCE_SENSOR_BASE + 0)));
    TEST_ASSERT_EQUAL_INT(1,
        static_cast<int>(events.countOf(EventType::AlarmRaised, EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + 0)));

    bus.setTemp(romA, 20.0f);  // replug
    senssvc_driveTicks(svc, now, 6);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(svc.state(0)));
    TEST_ASSERT_FALSE(svc.missing(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::SensorRecovered, EVENT_SOURCE_SENSOR_BASE + 0)));
    TEST_ASSERT_EQUAL_INT(1,
        static_cast<int>(events.countOf(EventType::AlarmCleared, EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + 0)));
}

static void senssvc_test_crc_error_faults_without_missing() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romB[8];
    FakeOneWireBus::makeRom(2, romB);
    bus.addDevice(romB);
    bus.setTemp(romB, 18.0f);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc.assign(1, romB, EventReason::Web, 0);
    uint64_t now = senssvc_driveTicks(svc, 0, 6);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(svc.state(1)));

    bus.setCrcError(romB);
    senssvc_driveTicks(svc, now, 6);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(svc.state(1)));
    TEST_ASSERT_FALSE(svc.missing(1));
    TEST_ASSERT_EQUAL_INT(0,
        static_cast<int>(events.countOf(EventType::AlarmRaised, EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + 1)));
}

static void senssvc_test_shorted_bus_all_assigned_missing_after_three_cycles() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8], romB[8];
    FakeOneWireBus::makeRom(1, romA);
    FakeOneWireBus::makeRom(2, romB);
    bus.addDevice(romA);
    bus.addDevice(romB);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc.assign(0, romA, EventReason::Web, 0);
    svc.assign(1, romB, EventReason::Web, 0);
    TEST_ASSERT_FALSE(svc.missing(0));  // both found in the boot scan
    TEST_ASSERT_FALSE(svc.missing(1));

    bus.busShorted = true;
    senssvc_driveTicks(svc, 0, 6);  // 3 request/read pairs, all NoResponse

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(svc.state(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(svc.state(1)));
    TEST_ASSERT_TRUE(svc.missing(0));
    TEST_ASSERT_TRUE(svc.missing(1));
}

static void senssvc_test_boot_scan_missing_address_kept_in_settings() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(romA, text);
    config.setText(SENS_IDX_A, text, EventReason::Boot, 0);  // pre-existing mapping, device not on the bus

    FakeOneWireBus bus;  // empty bus: romA is not present
    SensorService svc(bus, config, events);
    TEST_ASSERT_TRUE(svc.begin(SENSSVC_TEST_SENSORS, 3, 0));

    TEST_ASSERT_TRUE(svc.missing(0));  // missing right after the boot scan evaluation
    TEST_ASSERT_EQUAL_INT(1,
        static_cast<int>(events.countOf(EventType::AlarmRaised, EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + 0)));
    TEST_ASSERT_EQUAL_STRING(text, config.getText(SENS_IDX_A));  // address kept in settings
}

static void senssvc_test_scan_marks_assigned_and_new_devices() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8], romNew[8];
    FakeOneWireBus::makeRom(1, romA);
    FakeOneWireBus::makeRom(2, romNew);
    bus.addDevice(romA);
    bus.addDevice(romNew);
    bus.setTemp(romNew, 15.0f);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc.assign(0, romA, EventReason::Web, 0);
    uint64_t now = senssvc_driveTicks(svc, 0, 2);  // one request + one read cycle

    CommonState state{};
    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT8(2, state.oneWire.count);

    bool foundAssigned = false;
    bool foundNew = false;
    for (uint8_t i = 0; i < state.oneWire.count; ++i) {
        if (addressEquals(state.oneWire.address[i], romA)) {
            TEST_ASSERT_EQUAL_UINT8(0, state.oneWire.logical[i]);
            foundAssigned = true;
        }
        if (addressEquals(state.oneWire.address[i], romNew)) {
            TEST_ASSERT_EQUAL_UINT8(NO_LOGICAL_SENSOR, state.oneWire.logical[i]);
            TEST_ASSERT_TRUE(state.oneWire.tempValid[i]);
            TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, state.oneWire.tempC[i]);
            foundNew = true;
        }
    }
    TEST_ASSERT_TRUE(foundAssigned);
    TEST_ASSERT_TRUE(foundNew);
    TEST_ASSERT_EQUAL_INT(0,
        static_cast<int>(events.countOf(EventType::AlarmRaised, EVENT_SOURCE_ALARM_BASE + SENSOR_MISSING_ALARM_BIT_BASE + 1)));
    (void)now;
}

static void senssvc_test_scan_overflow_truncates_and_logs_diag_once_per_scan() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    for (uint32_t i = 0; i < 13; ++i) {
        uint8_t rom[8];
        FakeOneWireBus::makeRom(i + 1, rom);
        bus.addDevice(rom);
    }

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    CommonState state{};
    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT8(12, state.oneWire.count);
    TEST_ASSERT_TRUE(state.oneWire.overflow);
    TEST_ASSERT_EQUAL_INT(1,
        static_cast<int>(events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ONEWIRE_OVERFLOW)));

    svc.requestRescan();
    svc.tick(0);
    TEST_ASSERT_EQUAL_INT(2,
        static_cast<int>(events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ONEWIRE_OVERFLOW)));
}

// Invalid ROMs for the scan-filter tests (D17): a bad CRC8, and a valid-CRC
// ROM from a non-DS18B20 family (0x10 = DS18S20).
inline void senssvc_makeBadCrcRom(uint32_t serial, uint8_t out[8]) {
    FakeOneWireBus::makeRom(serial, out);
    out[7] = static_cast<uint8_t>(out[7] ^ 0x5A);
}

inline void senssvc_makeForeignFamilyRom(uint32_t serial, uint8_t out[8]) {
    FakeOneWireBus::makeRom(serial, out);
    out[0] = 0x10;
    out[7] = Ds18b20::crc8(out, 7);
}

static void senssvc_test_scan_drops_invalid_roms() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romValid[8], romBadCrc[8], romForeign[8];
    FakeOneWireBus::makeRom(1, romValid);
    senssvc_makeBadCrcRom(2, romBadCrc);
    senssvc_makeForeignFamilyRom(3, romForeign);
    TEST_ASSERT_FALSE(Ds18b20::isValidRom(romBadCrc));
    TEST_ASSERT_FALSE(Ds18b20::isValidRom(romForeign));
    bus.addDevice(romBadCrc);
    bus.addDevice(romValid);
    bus.addDevice(romForeign);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    CommonState state{};
    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT8(1, state.oneWire.count);
    TEST_ASSERT_TRUE(addressEquals(state.oneWire.address[0], romValid));
    TEST_ASSERT_FALSE(state.oneWire.overflow);
    TEST_ASSERT_EQUAL_INT(0,
        static_cast<int>(events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ONEWIRE_OVERFLOW)));
}

static void senssvc_test_scan_invalid_roms_do_not_take_slots_or_overflow() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    // Invalid ROMs first, so they would be searched before the valid ones.
    for (uint32_t i = 0; i < 3; ++i) {
        uint8_t bad[8];
        senssvc_makeBadCrcRom(100 + i, bad);
        bus.addDevice(bad);
    }
    uint8_t valid[12][8];
    for (uint32_t i = 0; i < 12; ++i) {
        FakeOneWireBus::makeRom(i + 1, valid[i]);
        bus.addDevice(valid[i]);
    }

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    CommonState state{};
    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT8(12, state.oneWire.count);  // all 12 valid devices kept
    for (uint8_t i = 0; i < 12; ++i) {
        TEST_ASSERT_TRUE(addressEquals(state.oneWire.address[i], valid[i]));
    }
    TEST_ASSERT_FALSE(state.oneWire.overflow);
    TEST_ASSERT_EQUAL_INT(0,
        static_cast<int>(events.countOf(EventType::DiagnosticWarning, EVENT_SOURCE_DIAG_BASE + DIAG_CODE_ONEWIRE_OVERFLOW)));
}

static void senssvc_test_rescan_picks_up_new_device_next_tick() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    CommonState state{};
    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT8(0, state.oneWire.count);

    uint8_t romNew[8];
    FakeOneWireBus::makeRom(9, romNew);
    bus.addDevice(romNew);

    svc.requestRescan();
    svc.tick(0);

    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT8(1, state.oneWire.count);
}

static void senssvc_test_assign_move_unchanged_invalid() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    bus.addDevice(romA);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    // Invalid ROM (bad CRC) -> Rejected.
    uint8_t badRom[8];
    memcpy(badRom, romA, 8);
    badRom[7] ^= 0xFF;
    TEST_ASSERT_TRUE(svc.assign(0, badRom, EventReason::Web, 0) == CommandStatus::Rejected);

    TEST_ASSERT_TRUE(svc.assign(0, romA, EventReason::Web, 0) == CommandStatus::Ok);
    TEST_ASSERT_TRUE(svc.assign(0, romA, EventReason::Web, 0) == CommandStatus::Unchanged);

    // Move: assigning the same address to logical 1 clears it from 0.
    TEST_ASSERT_TRUE(svc.assign(1, romA, EventReason::Web, 0) == CommandStatus::Ok);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unassigned), static_cast<int>(svc.state(0)));

    TEST_ASSERT_TRUE(svc.clear(1, EventReason::Web, 0) == CommandStatus::Ok);
    TEST_ASSERT_TRUE(svc.clear(1, EventReason::Web, 0) == CommandStatus::Unchanged);
    TEST_ASSERT_TRUE(svc.assign(2, nullptr, EventReason::Web, 0) == CommandStatus::Rejected);
}

static void senssvc_test_duplicate_addresses_via_setText_normalised_next_tick() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    bus.addDevice(romA);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc.assign(0, romA, EventReason::Web, 0);

    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(romA, text);
    config.setText(SENS_IDX_B, text, EventReason::Import, 0);  // as a backup import would

    svc.tick(0);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unknown), static_cast<int>(svc.state(0)));  // kept (lower index)
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unassigned), static_cast<int>(svc.state(1)));  // cleared
    TEST_ASSERT_EQUAL_STRING("", config.getText(SENS_IDX_B));
}

static void senssvc_test_invalid_text_cleared() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    config.setText(SENS_IDX_C, "XYZ", EventReason::Import, 0);
    svc.tick(0);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unassigned), static_cast<int>(svc.state(2)));
    TEST_ASSERT_EQUAL_STRING("", config.getText(SENS_IDX_C));
}

static void senssvc_test_reassignment_resets_debounce_to_unknown() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;
    uint8_t romA[8], romB[8];
    FakeOneWireBus::makeRom(1, romA);
    FakeOneWireBus::makeRom(2, romB);
    bus.addDevice(romA);
    bus.addDevice(romB);
    bus.setTemp(romA, 20.0f);
    bus.setTemp(romB, 25.0f);

    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc.assign(0, romA, EventReason::Web, 0);
    uint64_t now = senssvc_driveTicks(svc, 0, 6);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Ok), static_cast<int>(svc.state(0)));

    svc.assign(0, romB, EventReason::Web, now);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unknown), static_cast<int>(svc.state(0)));
}

static void senssvc_test_backup_roundtrip_preserves_mapping() {
    MemoryKvStore store1;
    RecordingEventSink events1;
    ConfigEngine config1(store1, events1);
    config1.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus1;
    uint8_t romA[8], romB[8];
    FakeOneWireBus::makeRom(1, romA);
    FakeOneWireBus::makeRom(2, romB);
    bus1.addDevice(romA);
    bus1.addDevice(romB);

    SensorService svc1(bus1, config1, events1);
    svc1.begin(SENSSVC_TEST_SENSORS, 3, 0);
    svc1.assign(0, romA, EventReason::Web, 0);
    svc1.assign(1, romB, EventReason::Web, 0);

    static char json[4096];
    size_t len = BackupCodec::exportJson(config1, "1.0.0", json, sizeof(json));
    TEST_ASSERT_TRUE(len > 0);

    MemoryKvStore store2;
    RecordingEventSink events2;
    ConfigEngine config2(store2, events2);
    config2.begin(SENSSVC_TEST_SCHEMA, 0);
    BackupImportResult result = BackupCodec::importJson(config2, events2, json, len, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(BackupStatus::Ok), static_cast<int>(result.status));

    FakeOneWireBus bus2;
    SensorService svc2(bus2, config2, events2);
    TEST_ASSERT_TRUE(svc2.begin(SENSSVC_TEST_SENSORS, 3, 0));

    CommonState state{};
    svc2.fillStatus(state);
    TEST_ASSERT_TRUE(state.sensors.sensor[0].assigned);
    TEST_ASSERT_TRUE(addressEquals(state.sensors.sensor[0].address, romA));
    TEST_ASSERT_TRUE(state.sensors.sensor[1].assigned);
    TEST_ASSERT_TRUE(addressEquals(state.sensors.sensor[1].address, romB));
}

static void senssvc_test_backup_import_duplicate_normalised_after_begin() {
    MemoryKvStore store1;
    RecordingEventSink events1;
    ConfigEngine config1(store1, events1);
    config1.begin(SENSSVC_TEST_SCHEMA, 0);
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(romA, text);
    // Duplicate written directly at the ConfigEngine level (bypassing
    // SensorService), the same way a hand-edited/corrupt backup file would.
    config1.setText(SENS_IDX_A, text, EventReason::Import, 0);
    config1.setText(SENS_IDX_B, text, EventReason::Import, 0);

    static char json[4096];
    size_t len = BackupCodec::exportJson(config1, "1.0.0", json, sizeof(json));
    TEST_ASSERT_TRUE(len > 0);

    MemoryKvStore store2;
    RecordingEventSink events2;
    ConfigEngine config2(store2, events2);
    config2.begin(SENSSVC_TEST_SCHEMA, 0);
    BackupCodec::importJson(config2, events2, json, len, 0);

    FakeOneWireBus bus2;
    SensorService svc2(bus2, config2, events2);
    TEST_ASSERT_TRUE(svc2.begin(SENSSVC_TEST_SENSORS, 3, 0));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unknown), static_cast<int>(svc2.state(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Unassigned), static_cast<int>(svc2.state(1)));
}

// Stage 04 (D15 OTA rollback health check input): readCycleCount() advances
// once per completed request/read pair (CYCLE_MS = 2000 ms), independent of
// whether any sensor is present/assigned/healthy.
static void senssvc_test_read_cycles_count_without_sensors() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    FakeOneWireBus bus;  // no devices at all

    SensorService svc(bus, config, events);
    TEST_ASSERT_TRUE(svc.begin(SENSSVC_TEST_SENSORS, 3, 0));  // no assignments either
    TEST_ASSERT_EQUAL_UINT32(0, svc.readCycleCount());

    const int N = 10;
    senssvc_driveTicks(svc, 0, N * 2);  // N * CYCLE_MS elapsed (1 s sub-ticks)

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(N), svc.readCycleCount());

    CommonState state{};
    svc.fillStatus(state);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(N), state.oneWire.readCycleCount);
}

static void senssvc_test_read_cycles_count_with_faulty_sensor() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(romA, text);
    config.setText(SENS_IDX_A, text, EventReason::Boot, 0);  // assigned, but never on the bus

    FakeOneWireBus bus;  // romA absent -> the assigned sensor stays missing/faulted
    SensorService svc(bus, config, events);
    TEST_ASSERT_TRUE(svc.begin(SENSSVC_TEST_SENSORS, 3, 0));
    TEST_ASSERT_TRUE(svc.missing(0));

    const int N = 5;
    senssvc_driveTicks(svc, 0, N * 2);

    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(N), svc.readCycleCount());
    TEST_ASSERT_TRUE(svc.missing(0));  // still faulted -- the cycle counter is not gated on sensor health
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SensorState::Fault), static_cast<int>(svc.state(0)));
}

static void senssvc_test_fill_status_preserves_other_alarm_bits() {
    MemoryKvStore store;
    RecordingEventSink events;
    ConfigEngine config(store, events);
    config.begin(SENSSVC_TEST_SCHEMA, 0);
    uint8_t romA[8];
    FakeOneWireBus::makeRom(1, romA);
    char text[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(romA, text);
    config.setText(SENS_IDX_A, text, EventReason::Boot, 0);  // not on the bus -> missing

    FakeOneWireBus bus;
    SensorService svc(bus, config, events);
    svc.begin(SENSSVC_TEST_SENSORS, 3, 0);

    CommonState state{};
    state.alarms.activeMask = 0x000000FFu;  // project-owned low bits
    svc.fillStatus(state);

    TEST_ASSERT_EQUAL_UINT32(0x000000FFu, state.alarms.activeMask & ~SENSOR_MISSING_ALARM_MASK);
    TEST_ASSERT_TRUE((state.alarms.activeMask & senssvc_missingBit(0)) != 0);
}

// Runs every test in this suite. Call from runHwSuite().
inline void runSensorServiceSuite() {
    RUN_TEST(senssvc_test_cycle_timing_request_then_read);
    RUN_TEST(senssvc_test_assigned_sensor_ok_after_three_reads_no_fault_at_boot);
    RUN_TEST(senssvc_test_unplug_faults_and_alarms_replug_recovers);
    RUN_TEST(senssvc_test_crc_error_faults_without_missing);
    RUN_TEST(senssvc_test_shorted_bus_all_assigned_missing_after_three_cycles);
    RUN_TEST(senssvc_test_boot_scan_missing_address_kept_in_settings);
    RUN_TEST(senssvc_test_scan_marks_assigned_and_new_devices);
    RUN_TEST(senssvc_test_scan_overflow_truncates_and_logs_diag_once_per_scan);
    RUN_TEST(senssvc_test_scan_drops_invalid_roms);
    RUN_TEST(senssvc_test_scan_invalid_roms_do_not_take_slots_or_overflow);
    RUN_TEST(senssvc_test_rescan_picks_up_new_device_next_tick);
    RUN_TEST(senssvc_test_assign_move_unchanged_invalid);
    RUN_TEST(senssvc_test_duplicate_addresses_via_setText_normalised_next_tick);
    RUN_TEST(senssvc_test_invalid_text_cleared);
    RUN_TEST(senssvc_test_reassignment_resets_debounce_to_unknown);
    RUN_TEST(senssvc_test_backup_roundtrip_preserves_mapping);
    RUN_TEST(senssvc_test_backup_import_duplicate_normalised_after_begin);
    RUN_TEST(senssvc_test_fill_status_preserves_other_alarm_bits);
    RUN_TEST(senssvc_test_read_cycles_count_without_sensors);
    RUN_TEST(senssvc_test_read_cycles_count_with_faulty_sensor);
}
