#pragma once
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/EventTypes.h"
#include "../lib/NetEngine/src/AtomicIndexSet.h"
#include "../lib/NetEngine/src/NetIdentity.h"
#include "../lib/NetEngine/src/VersionInfo.h"
#include "../lib/NetEngine/src/WifiCredsMailbox.h"
#include "../lib/NetEngine/src/WifiScanList.h"
#include "../lib/NetEngine/src/WifiSupervisor.h"
#include "fakes/RecordingEventSink.h"

// Native-safe Unity tests for stage 04 phase 3: NetIdentity (validation +
// toSnakeKey), VersionInfo (web version parse + fw/web mismatch),
// AtomicIndexSet, WifiScanList, and WifiSupervisor (the pure Wi-Fi decision
// machine, D3-D5). Header-only, run via NetSuite.h. Test names are prefixed
// wifi_ (D24).

namespace {

// Drives WifiSupervisor with a persistent WifiInputs the test mutates
// in-place between tick() calls (mirroring how ConnectivityRuntime would
// carry live port state across fast ticks).
struct WifiFixture {
    WifiSupervisor sup;
    RecordingEventSink events;
    WifiInputs in{};

    WifiActions begin(const char* ssid, const char* pass, uint64_t nowMs) {
        in = WifiInputs{ssid, pass, false, false, false, false};
        return sup.begin(ssid, pass, nowMs);
    }

    WifiActions tick(uint64_t nowMs) { return sup.tick(in, nowMs, events); }
};

}  // namespace

// ---- NetIdentity ----------------------------------------------------------

static void wifi_test_identity_valid_both_projects() {
    NetIdentity boilerRoom{"boiler-room", "boiler_room", "Boiler room", "KC868-A6 boiler-room", "BoilerRoom-Setup"};
    NetIdentity homeHeating{
        "home-heating", "home_heating", "Home heating", "KC868-A6 home-heating", "HomeHeating-Setup"};
    TEST_ASSERT_TRUE(validateNetIdentity(boilerRoom));
    TEST_ASSERT_TRUE(validateNetIdentity(homeHeating));
}

static void wifi_test_identity_rejects_generic_names() {
    NetIdentity generic1{"boiler", "boiler", "Boiler", "Boiler", "Boiler-Setup"};
    NetIdentity generic2{"home", "home", "Home", "Home", "Home-Setup"};
    TEST_ASSERT_FALSE(validateNetIdentity(generic1));
    TEST_ASSERT_FALSE(validateNetIdentity(generic2));
}

static void wifi_test_identity_rejects_uppercase() {
    NetIdentity badPrefix{"Boiler-Room", "boiler_room", "Boiler room", "Model", "Setup"};
    NetIdentity badUnique{"boiler-room", "Boiler_Room", "Boiler room", "Model", "Setup"};
    TEST_ASSERT_FALSE(validateNetIdentity(badPrefix));
    TEST_ASSERT_FALSE(validateNetIdentity(badUnique));
}

static void wifi_test_identity_rejects_empty() {
    NetIdentity emptyPrefix{"", "boiler_room", "Boiler room", "Model", "Setup"};
    NetIdentity emptyDevice{"boiler-room", "boiler_room", "", "Model", "Setup"};
    NetIdentity nullPrefix{nullptr, "boiler_room", "Boiler room", "Model", "Setup"};
    NetIdentity overLongPrefix{"a-very-long-prefix-over-24-chars", "boiler_room", "Boiler room", "Model", "Setup"};
    TEST_ASSERT_FALSE(validateNetIdentity(emptyPrefix));
    TEST_ASSERT_FALSE(validateNetIdentity(emptyDevice));
    TEST_ASSERT_FALSE(validateNetIdentity(nullPrefix));
    TEST_ASSERT_FALSE(validateNetIdentity(overLongPrefix));
}

static void wifi_test_snake_key_cases() {
    char buf[32];
    TEST_ASSERT_TRUE(toSnakeKey("relayLock", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("relay_lock", buf);

    TEST_ASSERT_TRUE(toSnakeKey("homeNoNeed", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("home_no_need", buf);

    TEST_ASSERT_TRUE(toSnakeKey("K1 power", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("k1_power", buf);

    TEST_ASSERT_TRUE(toSnakeKey("T1", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("t1", buf);
}

static void wifi_test_snake_key_overflow() {
    char buf[6] = "XXXXX";  // "relay_lock" needs 11 bytes incl. NUL
    TEST_ASSERT_FALSE(toSnakeKey("relayLock", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("XXXXX", buf);  // left untouched on failure

    char empty[8];
    TEST_ASSERT_FALSE(toSnakeKey("###", empty, sizeof(empty)));  // every char dropped -> empty result
    TEST_ASSERT_FALSE(toSnakeKey("", empty, sizeof(empty)));

    char exact[3];
    TEST_ASSERT_TRUE(toSnakeKey("T1", exact, sizeof(exact)));  // "t1\0" fits exactly
    TEST_ASSERT_EQUAL_STRING("t1", exact);
    TEST_ASSERT_FALSE(toSnakeKey("T1", exact, 2));  // one byte short
}

// ---- VersionInfo ------------------------------------------------------------

static void wifi_test_version_parse_trims_and_rejects() {
    char out[VERSION_TEXT_LEN + 1];

    const char* raw = "  1.2.3 \r\n";
    TEST_ASSERT_TRUE(parseWebVersionText(raw, strlen(raw), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("1.2.3", out);

    TEST_ASSERT_FALSE(parseWebVersionText("   \r\n", 5, out, sizeof(out)));  // empty after trim
    TEST_ASSERT_FALSE(parseWebVersionText("", 0, out, sizeof(out)));

    const char nonPrintable[] = {'1', '.', '2', static_cast<char>(0x01)};
    TEST_ASSERT_FALSE(parseWebVersionText(nonPrintable, 4, out, sizeof(out)));

    char small[4];
    const char* longRaw = "1.2.3-verylongsuffix";
    TEST_ASSERT_TRUE(parseWebVersionText(longRaw, strlen(longRaw), small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("1.2", small);  // truncated to fit cap=4 (3 chars + NUL)
}

static void wifi_test_version_mismatch() {
    VersionStatus vs{};

    fillVersionStatus(vs, "1.0.0", "");
    TEST_ASSERT_FALSE(vs.webMismatch);
    TEST_ASSERT_EQUAL_STRING("", vs.web);

    fillVersionStatus(vs, "1.0.0", "1.0.0");
    TEST_ASSERT_FALSE(vs.webMismatch);

    fillVersionStatus(vs, "1.0.0", "1.0.1");
    TEST_ASSERT_TRUE(vs.webMismatch);

    fillVersionStatus(vs, "1.0.0", nullptr);
    TEST_ASSERT_FALSE(vs.webMismatch);
    TEST_ASSERT_EQUAL_STRING("", vs.web);
}

// ---- AtomicIndexSet ---------------------------------------------------------

static void wifi_test_atomic_index_set_basic() {
    AtomicIndexSet set;
    uint32_t out[4] = {};
    TEST_ASSERT_FALSE(set.takeAll(out));  // nothing set yet

    set.set(5);
    set.set(40);
    set.set(127);
    TEST_ASSERT_TRUE(set.takeAll(out));
    TEST_ASSERT_EQUAL_UINT32(0x20u, out[0]);
    TEST_ASSERT_EQUAL_UINT32(0x100u, out[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, out[2]);
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, out[3]);

    TEST_ASSERT_FALSE(set.takeAll(out));  // drained by the previous takeAll
    TEST_ASSERT_EQUAL_UINT32(0u, out[0]);
}

static void wifi_test_atomic_index_set_out_of_range() {
    AtomicIndexSet set;
    set.set(AtomicIndexSet::CAPACITY);  // == CAPACITY: out of range, ignored
    set.set(1000);
    uint32_t out[4] = {};
    TEST_ASSERT_FALSE(set.takeAll(out));
}

// ---- WifiScanList -----------------------------------------------------------

static void wifi_test_scan_list_dedupe_sort_replace() {
    WifiScanList list;
    TEST_ASSERT_TRUE(list.add("Alpha", -60, true));
    TEST_ASSERT_TRUE(list.add("Beta", -40, false));
    TEST_ASSERT_TRUE(list.add("Gamma", -80, true));
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(list.count()));
    TEST_ASSERT_EQUAL_STRING("Beta", list.entry(0).ssid);   // -40 strongest
    TEST_ASSERT_EQUAL_STRING("Alpha", list.entry(1).ssid);  // -60
    TEST_ASSERT_EQUAL_STRING("Gamma", list.entry(2).ssid);  // -80 weakest

    TEST_ASSERT_FALSE(list.add("Alpha", -70, true));  // weaker duplicate: no change
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(list.count()));
    TEST_ASSERT_EQUAL_INT(-60, static_cast<int>(list.entry(1).rssi));

    TEST_ASSERT_TRUE(list.add("Gamma", -30, true));  // stronger duplicate: updates + re-sorts
    TEST_ASSERT_EQUAL_STRING("Gamma", list.entry(0).ssid);
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(list.count()));

    for (size_t i = 0; i < WIFI_SCAN_MAX + 4; ++i) {
        char ssid[16];
        snprintf(ssid, sizeof(ssid), "Net%u", static_cast<unsigned>(i));
        list.add(ssid, static_cast<int8_t>(-90 + static_cast<int>(i)), false);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WIFI_SCAN_MAX), static_cast<int>(list.count()));

    TEST_ASSERT_FALSE(list.add(nullptr, -50, false));
    TEST_ASSERT_FALSE(list.add("", -50, false));
}

// ---- WifiSupervisor ---------------------------------------------------------

static void wifi_test_boot_without_ssid_starts_ap_only() {
    WifiFixture f;
    WifiActions a = f.begin("", "", 0);
    TEST_ASSERT_TRUE(a.startAp);
    TEST_ASSERT_FALSE(a.connect);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupAp);
    TEST_ASSERT_TRUE(f.sup.apActive());
}

static void wifi_test_boot_with_ssid_connects_no_ap() {
    WifiFixture f;
    WifiActions a = f.begin("Home", "secret", 0);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_FALSE(a.startAp);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::Station);
    TEST_ASSERT_FALSE(f.sup.apActive());
}

static void wifi_test_unreachable_never_starts_ap_retries_10_to_30s() {
    WifiFixture f;
    f.begin("Home", "secret", 0);
    f.in.ssid = "Home";
    f.in.pass = "secret";

    const uint32_t gaps[] = {10000, 15000, 20000, 25000, 30000, 30000};
    uint64_t t = 0;
    for (size_t i = 0; i < 6; ++i) {
        t += gaps[i];
        WifiActions before = f.tick(t - 1);
        TEST_ASSERT_FALSE(before.connect);

        WifiActions fire = f.tick(t);
        TEST_ASSERT_TRUE(fire.connect);
        TEST_ASSERT_TRUE(fire.disconnect);
        TEST_ASSERT_FALSE(fire.startAp);
        TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::Station);
    }
}

static void wifi_test_short_drop_not_logged() {
    WifiFixture f;
    f.begin("Home", "secret", 0);
    f.in.ssid = "Home";
    f.in.pass = "secret";

    f.in.linkUp = true;
    f.tick(100);  // first connect, quick boot: never logged either way (D5)
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.events.countOf(EventType::WifiConnected)));

    f.in.linkUp = false;
    f.tick(1000);  // drop
    f.in.linkUp = true;
    f.tick(5000);  // reconnect well under OUTAGE_LOG_MS

    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.events.countOf(EventType::WifiDisconnected)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.events.countOf(EventType::WifiConnected)));
}

static void wifi_test_drop_over_30s_logs_disconnect_then_connect() {
    WifiFixture f;
    f.begin("Home", "secret", 0);
    f.in.ssid = "Home";
    f.in.pass = "secret";

    f.in.linkUp = true;
    f.tick(100);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.sup.connectCount()));

    f.in.linkUp = false;
    f.tick(1000);  // drop: outageStart = 1000

    f.tick(1000 + 29999);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.events.countOf(EventType::WifiDisconnected)));

    f.tick(1000 + 30000);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::WifiDisconnected)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.events.at(f.events.count() - 1).value);  // had linked before: value 0

    f.in.linkUp = true;
    f.tick(1000 + 30000 + 5000);  // reconnect 35 s after the drop
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::WifiConnected)));
    TEST_ASSERT_EQUAL_FLOAT(35.0f, f.events.at(f.events.count() - 1).value);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(f.sup.connectCount()));
}

static void wifi_test_boot_outage_logs_value_1() {
    WifiFixture f;
    f.begin("Home", "secret", 1000);
    f.in.ssid = "Home";
    f.in.pass = "secret";
    // never links

    f.tick(1000 + 29999);
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(f.events.countOf(EventType::WifiDisconnected)));

    f.tick(1000 + 30000);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(f.events.countOf(EventType::WifiDisconnected)));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, f.events.at(0).value);  // never linked: value 1
}

static void wifi_test_saving_creds_in_ap_joins_then_ap_off_after_linger() {
    WifiFixture f;
    f.begin("", "", 0);
    f.in.ssid = "";
    f.in.pass = "";
    f.in.apUp = true;
    f.tick(100);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupAp);

    f.in.ssid = "Home";
    f.in.pass = "secret";
    WifiActions saved = f.tick(1000);
    TEST_ASSERT_TRUE(saved.connect);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupApJoining);

    f.in.linkUp = true;
    f.tick(2000);  // joins: apStopAt = 2000 + AP_LINGER_MS(10000) = 12000
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupApJoining);
    TEST_ASSERT_TRUE(f.sup.apActive());

    WifiActions before = f.tick(11999);
    TEST_ASSERT_FALSE(before.stopAp);
    TEST_ASSERT_TRUE(f.sup.apActive());

    WifiActions off = f.tick(12000);
    TEST_ASSERT_TRUE(off.stopAp);
    TEST_ASSERT_FALSE(f.sup.apActive());
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::Station);
}

static void wifi_test_wrong_password_ap_off_after_join_window_no_return() {
    WifiFixture f;
    f.begin("", "", 0);
    f.in.ssid = "";
    f.in.pass = "";
    f.in.apUp = true;
    f.tick(100);

    f.in.ssid = "Home";
    f.in.pass = "wrong";
    f.tick(1000);  // joinDeadline = 1000 + AP_JOIN_WINDOW_MS(120000) = 121000
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupApJoining);
    // never links

    WifiActions before = f.tick(121000 - 1);
    TEST_ASSERT_FALSE(before.stopAp);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupApJoining);

    WifiActions off = f.tick(121000);
    TEST_ASSERT_TRUE(off.stopAp);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::Station);
    TEST_ASSERT_FALSE(f.sup.apActive());

    // Station-only from here: a saved-but-unreachable network never brings
    // the AP back (D4).
    f.in.apUp = false;
    WifiActions later = f.tick(121000 + 500000);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::Station);
    TEST_ASSERT_FALSE(later.startAp);
}

static void wifi_test_creds_change_while_connected_reconnects() {
    WifiFixture f;
    f.begin("Home", "old", 0);
    f.in.ssid = "Home";
    f.in.pass = "old";
    f.in.linkUp = true;
    f.tick(100);
    TEST_ASSERT_TRUE(f.sup.linkUp());

    f.in.pass = "new";
    WifiActions a = f.tick(1000);
    TEST_ASSERT_TRUE(a.disconnect);
    TEST_ASSERT_TRUE(a.connect);
    TEST_ASSERT_TRUE(a.linkWentDown);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::Station);
}

static void wifi_test_ssid_cleared_starts_ap() {
    WifiFixture f;
    f.begin("Home", "secret", 0);
    f.in.ssid = "Home";
    f.in.pass = "secret";
    f.in.linkUp = true;
    f.tick(100);

    f.in.ssid = "";
    f.in.pass = "";
    WifiActions a = f.tick(1000);
    TEST_ASSERT_TRUE(a.disconnect);
    TEST_ASSERT_TRUE(a.startAp);
    TEST_ASSERT_TRUE(f.sup.mode() == NetWifiMode::SetupAp);
    TEST_ASSERT_TRUE(f.sup.apActive());
}

static void wifi_test_scan_pauses_attempts_and_resumes() {
    WifiFixture f;
    f.begin("Home", "secret", 0);
    f.in.ssid = "Home";
    f.in.pass = "secret";  // never linked, still trying to connect

    f.in.scanRequested = true;
    WifiActions a = f.tick(500);
    TEST_ASSERT_TRUE(a.startScan);
    TEST_ASSERT_TRUE(a.disconnect);  // not linked: preceded by disconnect
    TEST_ASSERT_TRUE(f.sup.scanRunning());

    f.in.scanRequested = false;  // consumed by the caller
    // t=10000 is exactly when the normal retry (from begin()'s RETRY_FIRST_MS)
    // would have fired, and still well under SCAN_TIMEOUT_MS from the scan's
    // start (500) -- so this proves the retry is paused by the active scan,
    // not merely that the scan itself timed out.
    WifiActions during = f.tick(10000);
    TEST_ASSERT_FALSE(during.connect);  // retries paused while scanning
    TEST_ASSERT_TRUE(f.sup.scanRunning());

    f.in.scanFinished = true;
    WifiActions done = f.tick(21000);
    TEST_ASSERT_FALSE(f.sup.scanRunning());
    TEST_ASSERT_TRUE(done.connect);
}

static void wifi_test_scan_timeout() {
    WifiFixture f;
    f.begin("Home", "secret", 0);
    f.in.ssid = "Home";
    f.in.pass = "secret";

    f.in.scanRequested = true;
    f.tick(0);
    f.in.scanRequested = false;

    WifiActions before = f.tick(WifiSupervisor::SCAN_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(f.sup.scanRunning());
    TEST_ASSERT_FALSE(before.connect);

    WifiActions after = f.tick(WifiSupervisor::SCAN_TIMEOUT_MS);
    TEST_ASSERT_FALSE(f.sup.scanRunning());
    TEST_ASSERT_TRUE(after.connect);
}

static void wifi_test_ap_start_failure_retried() {
    WifiFixture f;
    WifiActions a0 = f.begin("", "", 0);
    TEST_ASSERT_TRUE(a0.startAp);
    f.in.ssid = "";
    f.in.pass = "";
    f.in.apUp = false;  // AP never actually comes up

    WifiActions before = f.tick(9999);
    TEST_ASSERT_FALSE(before.startAp);

    WifiActions retry1 = f.tick(10000);
    TEST_ASSERT_TRUE(retry1.startAp);

    WifiActions before2 = f.tick(19999);
    TEST_ASSERT_FALSE(before2.startAp);

    WifiActions retry2 = f.tick(20000);
    TEST_ASSERT_TRUE(retry2.startAp);
}

// ---- Wi-Fi credential input + mailbox (review-9 Must-fix 2 / Should-fix 4) --

static WifiCredsCheck wifiCheck(const char* ssid, const char* pass) {
    return checkWifiCreds(ssid, strlen(ssid), pass, strlen(pass));
}

static void wifi_test_creds_check_lengths() {
    char s32[33];
    memset(s32, 'a', 32);
    s32[32] = '\0';
    char s33[34];
    memset(s33, 'a', 33);
    s33[33] = '\0';
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::Ok, (uint8_t)wifiCheck("home", ""));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::Ok, (uint8_t)wifiCheck(s32, ""));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadSsid, (uint8_t)wifiCheck("", "password"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadSsid, (uint8_t)wifiCheck(s33, "password"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::Ok, (uint8_t)checkWifiCreds("home", 4, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadSsid, (uint8_t)checkWifiCreds(nullptr, 0, "", 0));
}

static void wifi_test_creds_check_password_rules() {
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadPass, (uint8_t)wifiCheck("home", "a"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadPass, (uint8_t)wifiCheck("home", "1234567"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::Ok, (uint8_t)wifiCheck("home", "12345678"));
    char p63[64];
    memset(p63, 'z', 63);
    p63[63] = '\0';
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::Ok, (uint8_t)wifiCheck("home", p63));
    char p64[65];
    memset(p64, 'A', 64);
    p64[64] = '\0';
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::Ok, (uint8_t)wifiCheck("home", p64));  // 64 hex
    p64[10] = 'g';
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadPass, (uint8_t)wifiCheck("home", p64));  // 64 non-hex
    char p65[66];
    memset(p65, 'a', 65);
    p65[65] = '\0';
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadPass, (uint8_t)wifiCheck("home", p65));
}

static void wifi_test_creds_check_rejects_embedded_nul() {
    const char ssid[] = {'h', 'o', '\0', 'e'};
    const char pass[] = {'1', '2', '3', '4', '\0', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadSsid, (uint8_t)checkWifiCreds(ssid, 4, "", 0));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WifiCredsCheck::BadPass, (uint8_t)checkWifiCreds("home", 4, pass, 9));
}

static void wifi_test_creds_mailbox_hands_over_both_fields() {
    WifiCredsMailbox box;
    WifiCreds out;
    TEST_ASSERT_FALSE(box.pending());
    TEST_ASSERT_FALSE(box.take(out));

    TEST_ASSERT_TRUE(box.offer("home", 4, "secret-pass", 11));
    TEST_ASSERT_TRUE(box.pending());
    TEST_ASSERT_TRUE(box.take(out));
    TEST_ASSERT_EQUAL_STRING("home", out.ssid);
    TEST_ASSERT_EQUAL_STRING("secret-pass", out.pass);
    TEST_ASSERT_FALSE(box.pending());
    TEST_ASSERT_FALSE(box.take(out));  // one-shot

    TEST_ASSERT_TRUE(box.offer("open-net", 8, "", 0));
    WifiCreds out2;
    TEST_ASSERT_TRUE(box.take(out2));
    TEST_ASSERT_EQUAL_STRING("open-net", out2.ssid);
    TEST_ASSERT_EQUAL_STRING("", out2.pass);  // previous password never leaks into an open-network pair
}

static void wifi_test_creds_mailbox_refuses_while_pending_and_invalid() {
    WifiCredsMailbox box;
    TEST_ASSERT_TRUE(box.offer("first", 5, "password1", 9));
    TEST_ASSERT_FALSE(box.offer("second", 6, "password2", 9));  // never overwrites a pending pair
    WifiCreds out;
    TEST_ASSERT_TRUE(box.take(out));
    TEST_ASSERT_EQUAL_STRING("first", out.ssid);
    TEST_ASSERT_EQUAL_STRING("password1", out.pass);

    TEST_ASSERT_FALSE(box.offer("home", 4, "short", 5));  // invalid: nothing stored
    TEST_ASSERT_FALSE(box.pending());
    TEST_ASSERT_FALSE(box.offer("", 0, "password", 8));
    TEST_ASSERT_FALSE(box.pending());
}

// Runs every test in this suite. Call from runNetSuite().
inline void runWifiSuite() {
    RUN_TEST(wifi_test_creds_check_lengths);
    RUN_TEST(wifi_test_creds_check_password_rules);
    RUN_TEST(wifi_test_creds_check_rejects_embedded_nul);
    RUN_TEST(wifi_test_creds_mailbox_hands_over_both_fields);
    RUN_TEST(wifi_test_creds_mailbox_refuses_while_pending_and_invalid);
    RUN_TEST(wifi_test_identity_valid_both_projects);
    RUN_TEST(wifi_test_identity_rejects_generic_names);
    RUN_TEST(wifi_test_identity_rejects_uppercase);
    RUN_TEST(wifi_test_identity_rejects_empty);
    RUN_TEST(wifi_test_snake_key_cases);
    RUN_TEST(wifi_test_snake_key_overflow);
    RUN_TEST(wifi_test_version_parse_trims_and_rejects);
    RUN_TEST(wifi_test_version_mismatch);
    RUN_TEST(wifi_test_atomic_index_set_basic);
    RUN_TEST(wifi_test_atomic_index_set_out_of_range);
    RUN_TEST(wifi_test_scan_list_dedupe_sort_replace);
    RUN_TEST(wifi_test_boot_without_ssid_starts_ap_only);
    RUN_TEST(wifi_test_boot_with_ssid_connects_no_ap);
    RUN_TEST(wifi_test_unreachable_never_starts_ap_retries_10_to_30s);
    RUN_TEST(wifi_test_short_drop_not_logged);
    RUN_TEST(wifi_test_drop_over_30s_logs_disconnect_then_connect);
    RUN_TEST(wifi_test_boot_outage_logs_value_1);
    RUN_TEST(wifi_test_saving_creds_in_ap_joins_then_ap_off_after_linger);
    RUN_TEST(wifi_test_wrong_password_ap_off_after_join_window_no_return);
    RUN_TEST(wifi_test_creds_change_while_connected_reconnects);
    RUN_TEST(wifi_test_ssid_cleared_starts_ap);
    RUN_TEST(wifi_test_scan_pauses_attempts_and_resumes);
    RUN_TEST(wifi_test_scan_timeout);
    RUN_TEST(wifi_test_ap_start_failure_retried);
}
