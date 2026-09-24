#pragma once
#include <unity.h>
#include <optional>
#include <stdint.h>
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/CoreEngine/src/HardwareStatus.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/HwEngine/src/RelayBank.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for RelayBank (stage 03 phase 2): the min ON/OFF
// lock, the three-slot arbitration (safety > exercise > control), the safety
// bypass, RelayChanged/RelayLockDelay logging (once per delay episode, D9),
// the output byte and the anti-seize timestamp bookkeeping. Header-only, run
// via HwSuite.h. Test names are prefixed relay_ (D24).

namespace {

// channel 0: pump, lockable, logs changes.
// channel 1: K1-power-like: not lockable, does not log changes.
// channel 2: pump, lockable, logs changes.
// channels 3-5 intentionally left unconfigured ("unused").
inline constexpr RelayChannelDesc RELAY_TEST_CHANNELS[] = {
    {0, "P1", RelayRole::Pump, true, true},
    {1, "K1pwr", RelayRole::K1Power, false, false},
    {2, "P2", RelayRole::Pump, true, true},
};

}  // namespace

static void relay_test_boot_lock_delays_first_on() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);

    TEST_ASSERT_TRUE(bank.requestControl(0, true));
    bank.update(1000, events);
    TEST_ASSERT_FALSE(bank.actual(0));
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.update(59999, events);
    TEST_ASSERT_FALSE(bank.actual(0));

    bank.update(60000, events);
    TEST_ASSERT_TRUE(bank.actual(0));
    TEST_ASSERT_FALSE(bank.lockDelayed(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::RelayChanged, EVENT_SOURCE_RELAY_BASE + 0)));
}

static void relay_test_min_on_delays_off_request() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);
    bank.requestControl(0, true);
    bank.update(0, events);
    TEST_ASSERT_TRUE(bank.actual(0));

    bank.setLockMs(60000);
    bank.requestControl(0, false);
    bank.update(10000, events);
    TEST_ASSERT_TRUE(bank.actual(0));  // still ON: min-ON not elapsed
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.update(60000, events);  // 60000ms since the ON switch at t=0
    TEST_ASSERT_FALSE(bank.actual(0));
}

static void relay_test_min_off_delays_on_request() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);
    bank.requestControl(0, true);
    bank.update(0, events);
    bank.requestControl(0, false);
    bank.update(1, events);
    TEST_ASSERT_FALSE(bank.actual(0));  // switched OFF at t=1

    bank.setLockMs(60000);
    bank.requestControl(0, true);
    bank.update(10000, events);
    TEST_ASSERT_FALSE(bank.actual(0));  // still OFF: min-OFF not elapsed
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.update(60001, events);  // 60000ms since the OFF switch at t=1
    TEST_ASSERT_TRUE(bank.actual(0));
}

static void relay_test_delayed_switch_applies_exactly_at_expiry() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(5000);
    bank.requestControl(0, true);

    bank.update(4999, events);
    TEST_ASSERT_FALSE(bank.actual(0));
    bank.update(5000, events);
    TEST_ASSERT_TRUE(bank.actual(0));
}

static void relay_test_withdrawn_request_during_delay_does_not_switch() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);
    bank.requestControl(0, true);
    bank.update(1000, events);
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.requestControl(0, false);  // withdrawn before the lock expires
    bank.update(60000, events);

    TEST_ASSERT_FALSE(bank.actual(0));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::RelayChanged, EVENT_SOURCE_RELAY_BASE + 0)));
}

static void relay_test_toggle_during_lock_logs_once_final_state_wins() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);

    bank.requestControl(0, true);
    bank.update(1000, events);  // delayed, logs once
    bank.requestControl(0, false);
    bank.update(2000, events);  // effective (false) == actual (false): not delayed, no log
    bank.requestControl(0, true);
    bank.update(3000, events);  // delayed again, but the episode's delayLogged flag was never reset -> no new log

    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::RelayLockDelay, EVENT_SOURCE_RELAY_BASE + 0)));

    bank.update(60000, events);
    TEST_ASSERT_TRUE(bank.actual(0));  // final requested state (true) wins
}

static void relay_test_lock_zero_is_immediate() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);
    bank.requestControl(0, true);
    bank.update(1, events);
    TEST_ASSERT_TRUE(bank.actual(0));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::RelayLockDelay)));
}

static void relay_test_lock_shortened_at_runtime_releases_delay() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);
    bank.requestControl(0, true);
    bank.update(1000, events);
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.setLockMs(10000);  // shortened while delayed
    bank.update(9999, events);
    TEST_ASSERT_FALSE(bank.actual(0));  // 9999ms since lastChangeMs(0) < 10000
    bank.update(10000, events);
    TEST_ASSERT_TRUE(bank.actual(0));
}

static void relay_test_safety_bypasses_pending_delay() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);
    bank.requestControl(0, true);
    bank.update(1000, events);
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.requestSafety(0, true);
    bank.update(1500, events);

    TEST_ASSERT_TRUE(bank.actual(0));
    TEST_ASSERT_FALSE(bank.lockDelayed(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(events.countOf(EventType::RelayLockDelay, EVENT_SOURCE_RELAY_BASE + 0)));

    const RecordingEventSink::Record& rec = events.at(events.count() - 1);
    TEST_ASSERT_EQUAL_UINT16(toU16(EventType::RelayChanged), rec.type);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(static_cast<uint8_t>(RelayReason::Safety)), rec.aux);

    // The safety switch restarts the lock window: a control OFF issued right
    // after clearing safety is delayed again, not applied immediately.
    bank.clearSafety(0);
    bank.requestControl(0, false);
    bank.update(1600, events);
    TEST_ASSERT_TRUE(bank.actual(0));
    TEST_ASSERT_TRUE(bank.lockDelayed(0));
}

static void relay_test_arbitration_safety_over_exercise_over_control() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);  // bypass so every arbitration change is visible immediately

    bank.requestControl(0, true);
    bank.update(0, events);
    TEST_ASSERT_TRUE(bank.actual(0));  // control alone

    bank.setExercise(0, false);
    bank.update(100, events);
    TEST_ASSERT_FALSE(bank.actual(0));  // exercise overrides control

    bank.requestSafety(0, true);
    bank.update(200, events);
    TEST_ASSERT_TRUE(bank.actual(0));  // safety overrides exercise and control

    bank.clearSafety(0);
    bank.update(300, events);
    TEST_ASSERT_FALSE(bank.actual(0));  // exercise still active, wins over control ON

    bank.setExercise(0, std::nullopt);
    bank.update(400, events);
    TEST_ASSERT_TRUE(bank.actual(0));  // back to plain control
}

static void relay_test_unused_channel_rejects_requests_stays_off() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);

    TEST_ASSERT_FALSE(bank.configured(5));
    TEST_ASSERT_FALSE(bank.requestControl(5, true));
    TEST_ASSERT_FALSE(bank.requestSafety(5, true));
    TEST_ASSERT_FALSE(bank.setExercise(5, true));

    bank.update(0, events);
    TEST_ASSERT_FALSE(bank.actual(5));
    TEST_ASSERT_EQUAL_UINT8(0, (bank.outputByte(false) >> 5) & 0x01);
}

static void relay_test_nonlockable_channel_switches_immediately_no_log() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);  // would delay a lockable channel

    bank.requestControl(1, true);  // channel 1: not lockable, does not log
    bank.update(1, events);

    TEST_ASSERT_TRUE(bank.actual(1));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::RelayChanged, EVENT_SOURCE_RELAY_BASE + 1)));
}

static void relay_test_output_byte_bits_and_polarity() {
    RelayBank bank;
    RecordingEventSink events;
    static constexpr RelayChannelDesc channels[] = {
        {0, "A", RelayRole::Pump, true, true},
        {3, "B", RelayRole::Pump, true, true},
    };
    bank.configure(channels, 2, 0);
    bank.setLockMs(0);

    TEST_ASSERT_EQUAL_UINT8(0xFF, bank.outputByte(true));
    TEST_ASSERT_EQUAL_UINT8(0x00, bank.outputByte(false));

    bank.requestControl(0, true);
    bank.requestControl(3, true);
    bank.update(0, events);

    TEST_ASSERT_EQUAL_UINT8(0x09, bank.outputByte(false));
    TEST_ASSERT_EQUAL_UINT8(0xF6, bank.outputByte(true));
}

static void relay_test_fill_status_fields() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(60000);
    bank.requestControl(0, true);
    bank.update(1000, events);

    RelayArray out{};
    bank.fillStatus(out, 30000);  // 30000ms remain of the 60000ms lock

    TEST_ASSERT_FALSE(out.on[0]);
    TEST_ASSERT_TRUE(out.channel[0].requested);
    TEST_ASSERT_TRUE(out.channel[0].lockDelayed);
    TEST_ASSERT_EQUAL_UINT16(30, out.channel[0].lockRemainingS);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RelayReason::Boot), static_cast<int>(out.channel[0].reason));
    TEST_ASSERT_FALSE(out.channel[0].safety);
}

static void relay_test_last_on_and_change_bookkeeping() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);

    TEST_ASSERT_FALSE(bank.hasBeenOn(0));
    TEST_ASSERT_FALSE(bank.hasChanged(0));

    bank.requestControl(0, true);
    bank.update(500, events);

    TEST_ASSERT_TRUE(bank.hasBeenOn(0));
    TEST_ASSERT_TRUE(bank.hasChanged(0));
    TEST_ASSERT_TRUE(bank.lastOnMs(0) == 500ull);
    TEST_ASSERT_TRUE(bank.lastChangeMs(0) == 500ull);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RelayReason::Control), static_cast<int>(bank.lastReason(0)));

    bank.update(1500, events);  // still ON: lastOnMs tracks "still on" time
    TEST_ASSERT_TRUE(bank.lastOnMs(0) == 1500ull);

    bank.requestControl(0, false);
    bank.update(2000, events);
    TEST_ASSERT_TRUE(bank.lastChangeMs(0) == 2000ull);
    TEST_ASSERT_TRUE(bank.lastOnMs(0) == 2000ull);  // OFF-switch time recorded too
}

static void relay_test_64bit_time_above_2_32_ms_no_wrap() {
    RelayBank bank;
    RecordingEventSink events;
    const uint64_t bootMs = 5000000000000ull;  // well above 2^32 ms (~136 years)
    bank.configure(RELAY_TEST_CHANNELS, 3, bootMs);
    bank.setLockMs(60000);

    bank.requestControl(0, true);
    bank.update(bootMs + 1000, events);
    TEST_ASSERT_FALSE(bank.actual(0));
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.update(bootMs + 60000, events);
    TEST_ASSERT_TRUE(bank.actual(0));
}

// --- Stage 04 D13: OTA output inhibit -------------------------------------

static void relay_test_inhibit_forces_all_off_and_logs() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);

    bank.requestControl(0, true);  // P1 via control
    bank.update(0, events);
    TEST_ASSERT_TRUE(bank.actual(0));

    bank.requestSafety(2, true);  // P2 via safety
    bank.update(100, events);
    TEST_ASSERT_TRUE(bank.actual(2));

    const size_t beforeCount = events.count();  // both channels already logged their boot->ON switch

    TEST_ASSERT_FALSE(bank.inhibited());
    bank.setInhibited(true, 200, events);

    TEST_ASSERT_TRUE(bank.inhibited());
    TEST_ASSERT_FALSE(bank.actual(0));
    TEST_ASSERT_FALSE(bank.actual(2));
    TEST_ASSERT_EQUAL_UINT8(0x00, bank.outputByte(false));  // all-OFF byte

    // Exactly 2 new RelayChanged logs (one per channel switched OFF by the inhibit).
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(events.count() - beforeCount));

    for (size_t i = beforeCount; i < events.count(); ++i) {
        const RecordingEventSink::Record& rec = events.at(i);
        TEST_ASSERT_EQUAL_UINT16(toU16(EventType::RelayChanged), rec.type);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, rec.value);  // OFF
        TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(static_cast<uint8_t>(RelayReason::Inhibit)), rec.aux);
    }
}

static void relay_test_inhibit_holds_off_against_requests() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);

    bank.setInhibited(true, 0, events);  // nothing was ON: no log
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::RelayChanged)));

    bank.requestControl(0, true);
    bank.requestSafety(2, true);
    bank.update(1000, events);

    TEST_ASSERT_FALSE(bank.actual(0));
    TEST_ASSERT_FALSE(bank.actual(2));
    TEST_ASSERT_FALSE(bank.lockDelayed(0));
    TEST_ASSERT_FALSE(bank.lockDelayed(2));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::RelayLockDelay)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(events.countOf(EventType::RelayChanged)));
}

static void relay_test_uninhibit_safety_immediate_control_waits_lock() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);

    bank.requestControl(0, true);  // P1
    bank.update(0, events);
    TEST_ASSERT_TRUE(bank.actual(0));

    bank.setLockMs(60000);

    bank.setInhibited(true, 1000, events);  // P1 forced OFF at t=1000
    TEST_ASSERT_FALSE(bank.actual(0));

    bank.setInhibited(false, 2000, events);  // release; lastChangeMs(0) stays 1000

    // Safety on P2 applies immediately, bypassing the lock.
    bank.requestSafety(2, true);
    bank.update(2100, events);
    TEST_ASSERT_TRUE(bank.actual(2));

    // P1's still-pending control request waits the 60s lock from the inhibit
    // OFF switch (t=1000), not from the release time (t=2000).
    bank.update(1000 + 59999, events);
    TEST_ASSERT_FALSE(bank.actual(0));
    TEST_ASSERT_TRUE(bank.lockDelayed(0));

    bank.update(1000 + 60000, events);
    TEST_ASSERT_TRUE(bank.actual(0));
}

static void relay_test_inhibit_idempotent() {
    RelayBank bank;
    RecordingEventSink events;
    bank.configure(RELAY_TEST_CHANNELS, 3, 0);
    bank.setLockMs(0);

    bank.requestControl(0, true);
    bank.update(0, events);
    TEST_ASSERT_TRUE(bank.actual(0));

    bank.setInhibited(true, 100, events);
    const size_t afterFirst = events.count();

    bank.setInhibited(true, 200, events);  // already inhibited: idempotent, no new log
    TEST_ASSERT_EQUAL_INT(static_cast<int>(afterFirst), static_cast<int>(events.count()));
    TEST_ASSERT_TRUE(bank.inhibited());
}

// Runs every test in this suite. Call from runHwSuite().
inline void runRelayBankSuite() {
    RUN_TEST(relay_test_boot_lock_delays_first_on);
    RUN_TEST(relay_test_min_on_delays_off_request);
    RUN_TEST(relay_test_min_off_delays_on_request);
    RUN_TEST(relay_test_delayed_switch_applies_exactly_at_expiry);
    RUN_TEST(relay_test_withdrawn_request_during_delay_does_not_switch);
    RUN_TEST(relay_test_toggle_during_lock_logs_once_final_state_wins);
    RUN_TEST(relay_test_lock_zero_is_immediate);
    RUN_TEST(relay_test_lock_shortened_at_runtime_releases_delay);
    RUN_TEST(relay_test_safety_bypasses_pending_delay);
    RUN_TEST(relay_test_arbitration_safety_over_exercise_over_control);
    RUN_TEST(relay_test_unused_channel_rejects_requests_stays_off);
    RUN_TEST(relay_test_nonlockable_channel_switches_immediately_no_log);
    RUN_TEST(relay_test_output_byte_bits_and_polarity);
    RUN_TEST(relay_test_fill_status_fields);
    RUN_TEST(relay_test_last_on_and_change_bookkeeping);
    RUN_TEST(relay_test_64bit_time_above_2_32_ms_no_wrap);
    RUN_TEST(relay_test_inhibit_forces_all_off_and_logs);
    RUN_TEST(relay_test_inhibit_holds_off_against_requests);
    RUN_TEST(relay_test_uninhibit_safety_immediate_control_waits_lock);
    RUN_TEST(relay_test_inhibit_idempotent);
}
