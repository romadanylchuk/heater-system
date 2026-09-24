#pragma once
#include <unity.h>
#include <stddef.h>
#include <stdint.h>
#include "../lib/CoreEngine/src/Command.h"
#include "../lib/CoreEngine/src/CommonSettings.h"
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/ConfigEngine.h"
#include "../lib/CoreEngine/src/ConfigSchema.h"
#include "../lib/CoreEngine/src/CoreRuntime.h"
#include "../lib/CoreEngine/src/EventLog.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/SettingDescriptor.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/HwRuntime.h"
#include "../lib/HwEngine/src/HwSettings.h"
#include "../lib/HwEngine/src/K1Driver.h"
#include "../lib/HwEngine/src/LocalTime.h"
#include "../lib/HwEngine/src/SensorAddress.h"
#include "fakes/FakeClock.h"
#include "fakes/FakeOneWireBus.h"
#include "fakes/FakeRelayPort.h"
#include "fakes/InMemoryCommandQueue.h"
#include "fakes/MemoryKvStore.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for HwRuntime (stage 03 phase 5, D2/D3/D11-D13/D22):
// the orchestration of RelayBank/K1Driver/SensorService/AntiSeizeScheduler
// behind RelayPort/OneWireBus, the relay port write policy, the K1
// power/direction interlock replayed at the actual output-byte level, and the
// AssignSensor/ClearSensor/RescanOneWire command path (both directly and
// through a real CoreRuntime extension handler). Header-only, run via
// HwSuite.h. Test names are prefixed hwrt_ (D24).

namespace {

// Home-heating-like test hardware config: K2 (Diverter) ch0, K1 power ch1,
// K1 direction ch2, P4 (Pump) ch3, 2 logical sensors, 3 anti-seize outputs
// (P4 Pump, K2 Toggle, K1 ValveStroke blocked by P4's channel) -- the same
// shape HOME_HEATING_HW uses, kept independent of it so this suite does not
// depend on the project package.
inline constexpr RelayChannelDesc HWRT_RELAYS[] = {
    {0, "K2", RelayRole::Diverter, true, true},
    {1, "K1 power", RelayRole::K1Power, false, false},
    {2, "K1 direction", RelayRole::K1Direction, false, false},
    {3, "P4", RelayRole::Pump, true, true},
};
inline constexpr LogicalSensorDesc HWRT_SENSORS[] = {
    {"H1", "sensorH1"},
    {"H2", "sensorH2"},
};
inline constexpr AntiSeizeOutputDesc HWRT_ANTI_SEIZE[] = {
    {"P4", AntiSeizeKind::Pump, 3, "asEnP4", NO_RELAY_CHANNEL},
    {"K2", AntiSeizeKind::Toggle, 0, "asEnK2", NO_RELAY_CHANNEL},
    {"K1", AntiSeizeKind::ValveStroke, 1, "asEnK1", 3},
};
inline constexpr K1Wiring HWRT_K1 = {true, 1, 2};
inline constexpr HwProjectConfig HWRT_CFG = {
    HWRT_RELAYS, 4,
    HWRT_SENSORS, 2,
    HWRT_ANTI_SEIZE, 3,
    HWRT_K1,
};

inline constexpr SettingDescriptor HWRT_PROJECT_SETTINGS[] = {
    textSetting("sensorH1", "sensH1", "Sensor H1", "Датчик H1", "sensors", 0, 16, ""),
    textSetting("sensorH2", "sensH2", "Sensor H2", "Датчик H2", "sensors", 0, 16, ""),
    boolSetting("asEnP4", "asEnP4", "Anti-seize P4", "Антизаклинювання P4", "antiSeize", true),
    boolSetting("asEnK2", "asEnK2", "Anti-seize K2", "Антизаклинювання K2", "antiSeize", true),
    boolSetting("asEnK1", "asEnK1", "Anti-seize K1", "Антизаклинювання K1", "antiSeize", true),
};
inline constexpr SettingsTable HWRT_PROJECT_TABLE = makeTable(HWRT_PROJECT_SETTINGS);
inline constexpr SettingsTable HWRT_SCHEMA_TABLES[] = {COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, HWRT_PROJECT_TABLE};
inline constexpr ConfigSchema HWRT_SCHEMA = {"test-hwrt", 1, HWRT_SCHEMA_TABLES, 3, nullptr, 0};

// Same shape, but missing the asEnK1 enable key -- for the SettingMissing test.
inline constexpr SettingDescriptor HWRT_PROJECT_SETTINGS_NO_K1_ENABLE[] = {
    textSetting("sensorH1", "sensH1", "Sensor H1", "Датчик H1", "sensors", 0, 16, ""),
    textSetting("sensorH2", "sensH2", "Sensor H2", "Датчик H2", "sensors", 0, 16, ""),
    boolSetting("asEnP4", "asEnP4", "Anti-seize P4", "Антизаклинювання P4", "antiSeize", true),
    boolSetting("asEnK2", "asEnK2", "Anti-seize K2", "Антизаклинювання K2", "antiSeize", true),
};
inline constexpr SettingsTable HWRT_PROJECT_TABLE_NO_K1_ENABLE = makeTable(HWRT_PROJECT_SETTINGS_NO_K1_ENABLE);
inline constexpr SettingsTable HWRT_SCHEMA_TABLES_NO_K1_ENABLE[] = {
    COMMON_SETTINGS_TABLE, HW_SETTINGS_TABLE, HWRT_PROJECT_TABLE_NO_K1_ENABLE};
inline constexpr ConfigSchema HWRT_SCHEMA_NO_K1_ENABLE = {
    "test-hwrt-no-k1en", 1, HWRT_SCHEMA_TABLES_NO_K1_ENABLE, 3, nullptr, 0};

// Fixture bundling a ConfigEngine (over a MemoryKvStore + RecordingEventSink)
// and an HwRuntime over a FakeRelayPort/FakeOneWireBus. Not used by the
// real-CoreRuntime command-path test, which needs a real EventLog instead.
struct HwrtFixture {
    MemoryKvStore cfgStore;
    RecordingEventSink events;
    ConfigEngine config;
    CommonState state{};
    FakeOneWireBus bus;
    FakeRelayPort port;
    HwRuntime hw;

    HwrtFixture() : config(cfgStore, events), hw(state, config, events, port, bus) {}

    bool beginConfig(const ConfigSchema& schema, uint64_t nowMs = 0) {
        return config.begin(schema, nowMs) == ConfigStatus::Ok;
    }
};

// Decodes RelayBank::outputByte(activeLow)'s encoding back to a per-channel
// boolean ("actual" state), the inverse of outputByte's `activeLow ? ~bits :
// bits`.
bool channelOnFromByte(uint8_t byte, uint8_t channel, bool activeLow) {
    const uint8_t bits = activeLow ? static_cast<uint8_t>(~byte) : byte;
    return ((bits >> channel) & 1) != 0;
}

}  // namespace

static void hwrt_test_begin_ok() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));
    TEST_ASSERT_TRUE(f.hw.ready());
}

static void hwrt_test_begin_missing_enable_key_setting_missing_and_all_off() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA_NO_K1_ENABLE));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(HwStatus::SettingMissing), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));
    TEST_ASSERT_FALSE(f.hw.ready());

    f.hw.fastTick(100);
    f.hw.fastTick(200);  // no change, no reassert due yet -- must not write again
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(f.port.written.size()));
    TEST_ASSERT_EQUAL_UINT8(0xFF, f.port.written[0]);  // RELAY_ACTIVE_LOW all-off byte
}

static void hwrt_test_begin_invalid_config_returns_invalid_config() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));

    static constexpr RelayChannelDesc badRelays[] = {
        {0, "P1", RelayRole::Pump, true, true},
        {0, "P2", RelayRole::Pump, true, true},  // duplicate channel -- invalid
    };
    static constexpr HwProjectConfig badCfg = {
        badRelays, 2, nullptr, 0, nullptr, 0, {false, NO_RELAY_CHANNEL, NO_RELAY_CHANNEL}};

    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::InvalidConfig), static_cast<int>(f.hw.begin(badCfg, 0)));
    TEST_ASSERT_FALSE(f.hw.ready());
}

static void hwrt_test_first_fast_tick_writes_all_off_byte() {
    HwrtFixture f;  // begin() never called
    f.hw.fastTick(0);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(f.port.written.size()));
    TEST_ASSERT_EQUAL_UINT8(0xFF, f.port.written[0]);
}

static void hwrt_test_port_write_only_on_change_and_reassert_after_5s() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    f.hw.fastTick(100);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(f.port.written.size()));

    f.hw.fastTick(200);
    f.hw.fastTick(4900);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(f.port.written.size()));  // unchanged, no reassert yet

    f.hw.fastTick(100 + RELAY_REASSERT_MS);  // exactly RELAY_REASSERT_MS after the first write
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(f.port.written.size()));
    TEST_ASSERT_EQUAL_UINT8(f.port.written[0], f.port.written[1]);  // same byte, re-asserted
}

static void hwrt_test_port_failure_sets_io_error_retries_and_recovers() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    f.port.failWrites = true;
    f.hw.fastTick(100);
    TEST_ASSERT_TRUE(f.state.relays.ioError);
    TEST_ASSERT_EQUAL_UINT32(1, f.state.relays.ioErrorCount);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(f.events.countOf(EventType::DiagnosticWarning)));

    f.hw.fastTick(200);  // still failing -- retried, but not re-logged
    TEST_ASSERT_TRUE(f.state.relays.ioError);
    TEST_ASSERT_EQUAL_UINT32(2, f.state.relays.ioErrorCount);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(f.events.countOf(EventType::DiagnosticWarning)));

    f.port.failWrites = false;
    f.hw.fastTick(300);
    TEST_ASSERT_FALSE(f.state.relays.ioError);
    TEST_ASSERT_EQUAL_UINT32(2, f.state.relays.ioErrorCount);  // unchanged on success
    TEST_ASSERT_EQUAL_UINT8(0xFF, f.port.last());              // still all-off (nothing requested ON)
}

static void hwrt_test_relay_lock_setting_change_takes_effect_next_tick() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));  // lock = 60s default

    TEST_ASSERT_TRUE(f.hw.relays().requestControl(3, true));  // P4 ON
    f.hw.fastTick(500);
    TEST_ASSERT_FALSE(f.hw.relays().actual(3));  // delayed by the 60s boot lock

    const int idxLock = f.config.indexOf(HW_KEY_RELAY_LOCK);
    TEST_ASSERT_TRUE(idxLock >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(f.config.setNumber(static_cast<size_t>(idxLock), 2, EventReason::Web, 500)));

    f.hw.tick(1000, NO_LOCAL_TIME);  // pushes the new 2s lock; 1000ms since boot < 2000ms, still delayed
    TEST_ASSERT_FALSE(f.hw.relays().actual(3));

    f.hw.tick(2100, NO_LOCAL_TIME);  // 2100ms since boot >= the new 2000ms lock
    TEST_ASSERT_TRUE(f.hw.relays().actual(3));
}

static void hwrt_test_k1_output_byte_never_changes_direction_while_power_on() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    uint64_t t = 0;
    f.hw.fastTick(t);  // establish the all-off baseline byte

    bool havePrev = false;
    bool prevPower = false;
    bool prevDir = false;
    bool sawPowerOn = false;
    bool sawDirFlip = false;
    uint64_t powerOnStart = 0;
    uint64_t powerOnEnd = 0;

    auto observeTick = [&](uint64_t nowMs, bool* flipSeenOut) {
        f.hw.fastTick(nowMs);
        const uint8_t byte = f.port.last();
        const bool power = channelOnFromByte(byte, HWRT_CFG.k1.powerChannel, RELAY_ACTIVE_LOW);
        const bool dir = channelOnFromByte(byte, HWRT_CFG.k1.directionChannel, RELAY_ACTIVE_LOW);
        if (havePrev && dir != prevDir) {
            if (flipSeenOut != nullptr) {
                *flipSeenOut = true;
            }
            TEST_ASSERT_FALSE_MESSAGE(prevPower, "direction bit changed on the tick right after power was ON");
            TEST_ASSERT_FALSE_MESSAGE(power, "direction bit changed on a tick where power is ON");
        }
        prevPower = power;
        prevDir = dir;
        havePrev = true;
        return power;
    };

    // First pulse: OPEN for 2000ms from rest. Proves the interlock holds at
    // the actual RelayPort output-byte level (K1 power ch1 / direction ch2)
    // while K1 power is continuously ON.
    TEST_ASSERT_TRUE(f.hw.k1().requestPulse(K1Direction::Open, 2000, t));
    for (int i = 0; i < 80; ++i) {  // 8s: covers dead time + the 2s run + the idle-rest dead time
        t += 100;
        const bool power = observeTick(t, nullptr);
        if (power && !sawPowerOn) {
            sawPowerOn = true;
            powerOnStart = t;
        }
        if (!power && sawPowerOn && powerOnEnd == 0) {
            powerOnEnd = t;
        }
    }
    TEST_ASSERT_TRUE(sawPowerOn);
    TEST_ASSERT_TRUE(powerOnEnd > powerOnStart);
    const uint64_t ranMs = powerOnEnd - powerOnStart;
    TEST_ASSERT_TRUE_MESSAGE(ranMs >= 1900 && ranMs <= 2100, "K1 power must stay ON for 2000ms +/- 100ms");

    // Second pulse: OPEN again, 5000ms, so a reversal can interrupt it mid-run.
    TEST_ASSERT_TRUE(f.hw.k1().requestPulse(K1Direction::Open, 5000, t));
    for (int i = 0; i < 25; ++i) {  // 2.5s: well past the dead time, still mid-run
        t += 100;
        observeTick(t, nullptr);
    }
    TEST_ASSERT_TRUE(f.hw.k1().powerOn());
    TEST_ASSERT_TRUE(f.hw.k1().directionOpen());

    // Reversal while running: OPEN -> CLOSE. The output byte must show power
    // OFF, then (>=1s later) the direction bit flip, then (>=1s later) power
    // back ON -- and at no point does the direction bit change while the
    // power bit is on.
    TEST_ASSERT_TRUE(f.hw.k1().requestPulse(K1Direction::Close, 2000, t));
    for (int i = 0; i < 60; ++i) {  // 6s: dead time + dir change + dead time + the 2s CLOSE run
        t += 100;
        observeTick(t, &sawDirFlip);
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDirFlip, "the reversal must actually flip the direction bit in the observed window");
    TEST_ASSERT_FALSE(f.hw.k1().powerOn());       // the CLOSE run has completed
    TEST_ASSERT_FALSE(f.hw.k1().directionOpen());  // ended de-energised (CLOSE)
}

static void hwrt_test_command_path_through_real_core_runtime() {
    MemoryKvStore cfgStore, logStore;
    FakeClock clock;
    EventLog log(logStore, clock);
    TEST_ASSERT_TRUE(log.begin());
    ConfigEngine config(cfgStore, log);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok), static_cast<int>(config.begin(HWRT_SCHEMA, 0)));

    CommonState state{};
    FakeOneWireBus bus;
    FakeRelayPort port;
    HwRuntime hw(state, config, log, port, bus);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(hw.begin(HWRT_CFG, 0)));

    InMemoryCommandQueue queue;
    CoreRuntime runtime(state, config, log, queue);
    runtime.setExtensionHandler(&HwRuntime::commandHook, &hw);

    const int idxH1 = config.indexOf("sensorH1");
    TEST_ASSERT_TRUE(idxH1 >= 0);

    uint8_t addr[8];
    FakeOneWireBus::makeRom(1, addr);
    TEST_ASSERT_TRUE(queue.post(makeAssignSensor(0, addr, EventReason::Web, 1)));
    runtime.tick(0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::Ok), state.system.lastCommandStatus);
    char expected[SENSOR_ADDRESS_TEXT_LEN + 1];
    formatAddress(addr, expected);
    TEST_ASSERT_EQUAL_STRING(expected, config.getText(static_cast<size_t>(idxH1)));

    TEST_ASSERT_TRUE(queue.post(makeClearSensor(0, EventReason::Web, 2)));
    runtime.tick(0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::Ok), state.system.lastCommandStatus);
    TEST_ASSERT_EQUAL_STRING("", config.getText(static_cast<size_t>(idxH1)));

    const size_t searchBefore = bus.searchCount;
    TEST_ASSERT_TRUE(queue.post(makeRescanOneWire(EventReason::Web, 3)));
    runtime.tick(1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::Ok), state.system.lastCommandStatus);
    hw.tick(2000, NO_LOCAL_TIME);  // the rescan actually runs on the next HW tick
    TEST_ASSERT_TRUE(bus.searchCount > searchBefore);

    TEST_ASSERT_TRUE(queue.post(makeAssignSensor(9, addr, EventReason::Web, 4)));  // only 2 sensors configured
    runtime.tick(3000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandStatus::InvalidCommand), state.system.lastCommandStatus);
}

static void hwrt_test_state_populated_after_tick() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    f.hw.tick(1000, NO_LOCAL_TIME);

    TEST_ASSERT_EQUAL_UINT8(2, f.state.sensors.count);
    TEST_ASSERT_TRUE(f.state.oneWire.done);
    TEST_ASSERT_TRUE(f.state.k1.present);
    TEST_ASSERT_EQUAL_UINT8(3, f.state.antiSeize.count);
    for (uint8_t ch = 0; ch < RELAY_CHANNEL_COUNT; ++ch) {
        TEST_ASSERT_FALSE(f.state.relays.on[ch]);  // nothing requested ON yet
    }
}

static void hwrt_test_anti_seize_enable_setting_flows_from_config() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    f.hw.tick(1000, NO_LOCAL_TIME);
    TEST_ASSERT_TRUE(f.state.antiSeize.output[1].enabled);  // HWRT_ANTI_SEIZE[1] == K2, default ON

    const int idxEnK2 = f.config.indexOf("asEnK2");
    TEST_ASSERT_TRUE(idxEnK2 >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(f.config.setNumber(static_cast<size_t>(idxEnK2), 0, EventReason::Web, 1000)));  // false

    f.hw.tick(2000, NO_LOCAL_TIME);
    TEST_ASSERT_FALSE(f.state.antiSeize.output[1].enabled);
    TEST_ASSERT_TRUE(f.state.antiSeize.output[0].enabled);   // P4 untouched
    TEST_ASSERT_TRUE(f.state.antiSeize.output[2].enabled);   // K1 untouched

    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(f.config.setNumber(static_cast<size_t>(idxEnK2), 1, EventReason::Web, 2000)));  // back ON
    f.hw.tick(3000, NO_LOCAL_TIME);
    TEST_ASSERT_TRUE(f.state.antiSeize.output[1].enabled);
}

// --- Stage 04 D13: OTA output inhibit -------------------------------------

static void hwrt_test_inhibit_writes_all_off_and_cancels_k1() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    uint64_t t = 0;
    f.hw.fastTick(t);  // establish the all-off baseline byte

    TEST_ASSERT_TRUE(f.hw.k1().requestPulse(K1Direction::Open, 5000, t));
    t += 2000;
    f.hw.fastTick(t);
    TEST_ASSERT_TRUE(f.hw.k1().busy());  // mid-run: past dead time, well before the 5s pulse ends

    f.hw.setOutputsInhibited(true, t);
    TEST_ASSERT_FALSE(f.hw.k1().busy());  // cancelled immediately, on the loop task, by setOutputsInhibited()

    t += 100;
    f.hw.fastTick(t);

    TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, f.port.last());
    TEST_ASSERT_TRUE(f.state.relays.inhibited);
    TEST_ASSERT_FALSE(f.state.k1.busy);
}

static void hwrt_test_inhibit_keeps_sensor_cycles() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    f.hw.setOutputsInhibited(true, 0);
    TEST_ASSERT_TRUE(f.hw.outputsInhibited());

    const uint32_t before = f.hw.sensors().readCycleCount();

    uint64_t t = 0;
    for (int i = 0; i < 10; ++i) {
        t += 1000;
        f.hw.tick(t, NO_LOCAL_TIME);
        TEST_ASSERT_TRUE(f.hw.outputsInhibited());  // still inhibited throughout
    }

    TEST_ASSERT_TRUE(f.hw.sensors().readCycleCount() > before);
    TEST_ASSERT_EQUAL_UINT32(f.hw.sensors().readCycleCount(), f.state.oneWire.readCycleCount);
}

static void hwrt_test_uninhibit_restores_control_after_lock() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));  // boot at t=0

    const int idxLock = f.config.indexOf(HW_KEY_RELAY_LOCK);
    TEST_ASSERT_TRUE(idxLock >= 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ConfigStatus::Ok),
        static_cast<int>(f.config.setNumber(static_cast<size_t>(idxLock), 5, EventReason::Web, 0)));  // 5s lock

    TEST_ASSERT_TRUE(f.hw.relays().requestControl(3, true));  // P4 ON
    f.hw.tick(6000, NO_LOCAL_TIME);  // pushes the 5s lock; 6000ms since boot >= 5000ms: P4 turns ON
    TEST_ASSERT_TRUE(f.hw.relays().actual(3));

    f.hw.setOutputsInhibited(true, 7000);
    TEST_ASSERT_FALSE(f.hw.relays().actual(3));

    f.hw.setOutputsInhibited(false, 8000);

    // The still-pending control request waits the 5s lock from the inhibit OFF
    // switch (t=7000), not from the release time (t=8000).
    f.hw.tick(7000 + 4999, NO_LOCAL_TIME);
    TEST_ASSERT_FALSE(f.hw.relays().actual(3));

    f.hw.tick(7000 + 5000, NO_LOCAL_TIME);
    TEST_ASSERT_TRUE(f.hw.relays().actual(3));
}

static void hwrt_test_inhibit_before_ready_is_harmless() {
    HwrtFixture f;  // begin() never called: not ready

    f.hw.setOutputsInhibited(true, 0);
    TEST_ASSERT_TRUE(f.hw.outputsInhibited());

    f.hw.fastTick(100);
    TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, f.port.last());

    f.hw.setOutputsInhibited(false, 200);
    TEST_ASSERT_FALSE(f.hw.outputsInhibited());

    f.hw.fastTick(300);
    TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, f.port.last());
}

// A pre-ready inhibit request must be reconciled into RelayBank the instant
// begin() makes the runtime ready (review-2 must-fix): setOutputsInhibited()
// only stores the flag while !_ready, so begin() has to apply it explicitly.
static void hwrt_test_pending_inhibit_applied_when_begin_becomes_ready() {
    HwrtFixture f;
    TEST_ASSERT_TRUE(f.beginConfig(HWRT_SCHEMA));

    f.hw.setOutputsInhibited(true, 0);  // requested while not ready: flag-only
    TEST_ASSERT_TRUE(f.hw.outputsInhibited());

    TEST_ASSERT_EQUAL_INT(static_cast<int>(HwStatus::Ok), static_cast<int>(f.hw.begin(HWRT_CFG, 0)));

    // The fix: RelayBank/state must already report inhibited right out of begin().
    TEST_ASSERT_TRUE(f.hw.relays().inhibited());
    TEST_ASSERT_TRUE(f.state.relays.inhibited);

    // A control request (K2, ch0) and a safety request (P4, ch3) must both stay
    // OFF in the written port byte over several fast ticks -- normal
    // arbitration must not resume just because begin() succeeded.
    TEST_ASSERT_TRUE(f.hw.relays().requestControl(0, true));
    TEST_ASSERT_TRUE(f.hw.relays().requestSafety(3, true));

    uint64_t t = 0;
    for (int i = 0; i < 5; ++i) {
        t += 100;
        f.hw.fastTick(t);
        TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, f.port.last());
        TEST_ASSERT_FALSE(f.hw.relays().actual(0));
        TEST_ASSERT_FALSE(f.hw.relays().actual(3));
    }

    // K1 must not be driven either: a pulse requested while inhibited is
    // accepted by K1Driver itself (it does not know about the inhibit), but
    // fastTick's inhibited branch cancels it right back out instead of
    // ticking/replaying it into RelayBank, so it never actually runs.
    TEST_ASSERT_TRUE(f.hw.k1().requestPulse(K1Direction::Open, 2000, t));
    TEST_ASSERT_TRUE(f.hw.k1().busy());
    t += 100;
    f.hw.fastTick(t);
    TEST_ASSERT_FALSE(f.hw.k1().busy());
    TEST_ASSERT_FALSE(f.hw.k1().powerOn());
    TEST_ASSERT_EQUAL_UINT8(RELAY_ALL_OFF_BYTE, f.port.last());

    // Suggestion: the anti-seize scheduler must not run while inhibited
    // either -- jump well past the default 7-day interval (clock invalid, so
    // the uptime trigger applies) and confirm the K2 toggle output (anti-seize
    // index 1, unblocked by the control/safety requests above) stays idle
    // instead of starting, because tick() skips _antiSeize.tick() entirely
    // while outputsInhibited() is true.
    const uint64_t pastIntervalMs = 7ull * 86400000ull + 1000;
    f.hw.tick(pastIntervalMs, NO_LOCAL_TIME);
    TEST_ASSERT_FALSE(f.hw.antiSeize().pending(1));
    TEST_ASSERT_FALSE(f.hw.antiSeize().running(1));

    // Release: normal arbitration resumes. The safety request bypasses the
    // lock and turns ON immediately.
    f.hw.setOutputsInhibited(false, pastIntervalMs);
    f.hw.fastTick(pastIntervalMs + 100);
    TEST_ASSERT_FALSE(f.hw.relays().inhibited());
    TEST_ASSERT_FALSE(f.state.relays.inhibited);
    TEST_ASSERT_TRUE(f.hw.relays().actual(3));  // safety, bypasses the lock
}

// Runs every test in this suite. Call from runHwSuite().
inline void runHwRuntimeSuite() {
    RUN_TEST(hwrt_test_begin_ok);
    RUN_TEST(hwrt_test_begin_missing_enable_key_setting_missing_and_all_off);
    RUN_TEST(hwrt_test_begin_invalid_config_returns_invalid_config);
    RUN_TEST(hwrt_test_first_fast_tick_writes_all_off_byte);
    RUN_TEST(hwrt_test_port_write_only_on_change_and_reassert_after_5s);
    RUN_TEST(hwrt_test_port_failure_sets_io_error_retries_and_recovers);
    RUN_TEST(hwrt_test_relay_lock_setting_change_takes_effect_next_tick);
    RUN_TEST(hwrt_test_k1_output_byte_never_changes_direction_while_power_on);
    RUN_TEST(hwrt_test_command_path_through_real_core_runtime);
    RUN_TEST(hwrt_test_state_populated_after_tick);
    RUN_TEST(hwrt_test_anti_seize_enable_setting_flows_from_config);
    RUN_TEST(hwrt_test_inhibit_writes_all_off_and_cancels_k1);
    RUN_TEST(hwrt_test_inhibit_keeps_sensor_cycles);
    RUN_TEST(hwrt_test_uninhibit_restores_control_after_lock);
    RUN_TEST(hwrt_test_inhibit_before_ready_is_harmless);
    RUN_TEST(hwrt_test_pending_inhibit_applied_when_begin_becomes_ready);
}
