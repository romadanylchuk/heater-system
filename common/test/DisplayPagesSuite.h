#pragma once
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/DisplayEngine/src/DisplayFormat.h"
#include "../lib/DisplayEngine/src/DisplayFrame.h"
#include "../lib/DisplayEngine/src/DisplayPages.h"

// Native-safe Unity tests for DisplayPages (stage 06 phase 3): the page
// registry, the shared Network/Sensors pages, the Alarms page and the
// setup/reset/OTA/splash screens, plus the content-box layout invariant for
// every renderer. Header-only, run via DisplaySuite.h. Test names are
// prefixed dpg_.

static_assert(sizeof(DisplayFrame) < 400, "DisplayFrame must stay under 400 bytes");

// ---- helpers ----

static const LogicalSensorDesc DPG_SENSORS[] = {
    {"T1", "sensorT1"}, {"T2", "sensorT2"}, {"T3", "sensorT3"}, {"T4", "sensorT4"},
    {"T5", "sensorT5"}, {"T6", "sensorT6"}, {"T7", "sensorT7"}, {"T8", "sensorT8"},
};

static const AlarmDescriptor DPG_ALARMS[] = {
    {"ovh", "Boiler overheat", "Boiler overheat"},
};

static DisplayLabels dpg_labels(size_t sensorCount = 8) {
    DisplayLabels l = {};
    l.alarms = nullptr;
    l.alarmCount = 0;
    l.sensors = DPG_SENSORS;
    l.sensorCount = sensorCount;
    return l;
}

// Returns the text of the first line at this baseline and x, or nullptr.
static const char* dpg_textAt(const DisplayFrame& f, uint8_t baseline, uint8_t x = 0) {
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        if (f.line[i].baseline == baseline && f.line[i].x == x) {
            return f.line[i].text;
        }
    }
    return nullptr;
}

static const char* dpg_title(const DisplayFrame& f) {
    return dpg_textAt(f, DISPLAY_TITLE_BASELINE);
}

static const char* dpg_body(const DisplayFrame& f, uint8_t row, uint8_t x = 0) {
    return dpg_textAt(f, DISPLAY_BODY_BASELINE[row], x);
}

// Every line and the bar stay inside the 126x62 content box.
static void dpg_assertLayout(const DisplayFrame& f) {
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        const DisplayTextLine& l = f.line[i];
        const DisplayFontMetrics m = displayFontMetrics(l.font);
        TEST_ASSERT_TRUE_MESSAGE(l.x + strlen(l.text) * m.advance <= DISPLAY_CONTENT_WIDTH, l.text);
        TEST_ASSERT_TRUE_MESSAGE(l.baseline >= m.ascent, l.text);
        TEST_ASSERT_TRUE_MESSAGE(l.baseline + m.descent <= DISPLAY_CONTENT_HEIGHT - 1, l.text);
    }
    if (f.bar.used) {
        TEST_ASSERT_TRUE(f.bar.x + f.bar.w <= DISPLAY_CONTENT_WIDTH);
        TEST_ASSERT_TRUE(f.bar.y + f.bar.h <= DISPLAY_CONTENT_HEIGHT);
    }
}

static void dpg_testRender(const CommonState& s, DisplayFrame& f, void* ctx) {
    (void)s;
    (void)ctx;
    f.clear();
    f.setTitle("CONTROLLER");
}

static void dpg_connected(CommonState& s) {
    s.network.wifiConnected = true;
    s.network.wifiMode = NetWifiMode::Station;
    s.network.wifiRssi = -67;
    strcpy(s.network.ssid, "HomeNet");
    strcpy(s.network.ip, "192.168.1.42");
    s.network.mqttEnabled = true;
    s.network.mqttConnected = true;
}

// ---- registry ----

static void dpg_test_list_capacity() {
    DisplayPageList list;
    const DisplayPageDesc page = {"p", dpg_testRender, nullptr};
    for (size_t i = 0; i < DISPLAY_MAX_PAGES; ++i) {
        TEST_ASSERT_TRUE(list.add(page));
    }
    TEST_ASSERT_FALSE(list.add(page));
    TEST_ASSERT_EQUAL_UINT32(8, list.count());
    TEST_ASSERT_NULL(list.at(8));
    TEST_ASSERT_NOT_NULL(list.at(7));
}

static void dpg_test_list_rejects_null_render() {
    DisplayPageList list;
    const DisplayPageDesc bad = {"bad", nullptr, nullptr};
    TEST_ASSERT_FALSE(list.add(bad));
    TEST_ASSERT_EQUAL_UINT32(0, list.count());
    TEST_ASSERT_NULL(list.at(0));
}

static void dpg_test_append_shared_order() {
    DisplayPageList list;
    const DisplayLabels labels = dpg_labels();
    const DisplayPageDesc ctrl = {"Boiler", dpg_testRender, nullptr};
    TEST_ASSERT_TRUE(list.add(ctrl));
    TEST_ASSERT_TRUE(appendSharedPages(list, &labels));
    TEST_ASSERT_EQUAL_UINT32(3, list.count());
    TEST_ASSERT_TRUE(list.at(0)->render == dpg_testRender);
    TEST_ASSERT_TRUE(list.at(1)->render == renderNetworkPage);
    TEST_ASSERT_TRUE(list.at(2)->render == renderSensorsPage);
    TEST_ASSERT_TRUE(list.at(2)->ctx == &labels);

    // The controller page registered first renders first.
    CommonState s{};
    DisplayFrame f{};
    list.at(0)->render(s, f, list.at(0)->ctx);
    TEST_ASSERT_EQUAL_STRING("CONTROLLER", dpg_title(f));
}

static void dpg_test_append_shared_no_room() {
    DisplayPageList list;
    const DisplayPageDesc page = {"p", dpg_testRender, nullptr};
    for (size_t i = 0; i < DISPLAY_MAX_PAGES - 1; ++i) {
        list.add(page);
    }
    TEST_ASSERT_FALSE(appendSharedPages(list, nullptr));
    TEST_ASSERT_EQUAL_UINT32(DISPLAY_MAX_PAGES - 1, list.count());  // all or nothing
}

static void dpg_test_subpage_count() {
    TEST_ASSERT_EQUAL_UINT8(0, alarmSubPageCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, alarmSubPageCount(0x1F));
    TEST_ASSERT_EQUAL_UINT8(2, alarmSubPageCount(0x3F));
    TEST_ASSERT_EQUAL_UINT8(7, alarmSubPageCount(0xFFFFFFFFu));
}

// ---- Network ----

static void dpg_test_network_connected() {
    CommonState s{};
    dpg_connected(s);
    DisplayFrame f{};
    renderNetworkPage(s, f, nullptr);
    TEST_ASSERT_EQUAL_STRING("NETWORK", dpg_title(f));
    TEST_ASSERT_TRUE(f.titleRule);
    TEST_ASSERT_EQUAL_STRING("WiFi: connected", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("SSID: HomeNet", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("RSSI: -67 dBm", dpg_body(f, 2));
    TEST_ASSERT_EQUAL_STRING("IP: 192.168.1.42", dpg_body(f, 3));
    TEST_ASSERT_EQUAL_STRING("MQTT: connected", dpg_body(f, 4));
    TEST_ASSERT_EQUAL_UINT8(6, f.lineCount);
    TEST_ASSERT_TRUE(f.line[1].font == DisplayFont::Normal);
}

static void dpg_test_network_connecting() {
    CommonState s{};
    s.network.wifiMode = NetWifiMode::Station;
    strcpy(s.network.ssid, "HomeNet");
    strcpy(s.network.ip, "10.0.0.2");  // stale IP must not show while disconnected
    s.network.mqttEnabled = true;
    DisplayFrame f{};
    renderNetworkPage(s, f, nullptr);
    TEST_ASSERT_EQUAL_STRING("WiFi: connecting", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("SSID: HomeNet", dpg_body(f, 1));
    TEST_ASSERT_NULL(dpg_body(f, 2));
    TEST_ASSERT_EQUAL_STRING("IP: --", dpg_body(f, 3));
    TEST_ASSERT_EQUAL_STRING("MQTT: disconnected", dpg_body(f, 4));
}

static void dpg_test_network_setup_ap_mqtt_disabled() {
    CommonState s{};
    s.network.setupApActive = true;
    s.network.wifiMode = NetWifiMode::SetupAp;
    s.network.mqttEnabled = false;
    DisplayFrame f{};
    renderNetworkPage(s, f, nullptr);
    TEST_ASSERT_EQUAL_STRING("WiFi: setup AP", dpg_body(f, 0));
    TEST_ASSERT_NULL(dpg_body(f, 1));
    TEST_ASSERT_NULL(dpg_body(f, 2));
    TEST_ASSERT_EQUAL_STRING("IP: --", dpg_body(f, 3));
    TEST_ASSERT_EQUAL_STRING("MQTT: disabled", dpg_body(f, 4));
}

static void dpg_test_network_long_ssid_and_ip() {
    CommonState s{};
    dpg_connected(s);
    strcpy(s.network.ssid, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");  // 32 chars
    strcpy(s.network.ip, "255.255.255.255");                    // 15 chars
    DisplayFrame f{};
    renderNetworkPage(s, f, nullptr);
    TEST_ASSERT_EQUAL_STRING("SSID: ABCDEFGHIJKLMN~", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("IP: 255.255.255.255", dpg_body(f, 3));
}

// ---- Sensors ----

static void dpg_setSensor(CommonState& s, uint8_t i, bool assigned, bool missing, SensorState st) {
    s.sensors.sensor[i].assigned = assigned;
    s.sensors.sensor[i].missing = missing;
    s.sensors.sensor[i].state = st;
    s.sensors.sensor[i].tempC = 45.2f;
}

static void dpg_test_sensors_mixed() {
    CommonState s{};
    s.oneWire.done = true;
    s.oneWire.scanCount = 1;
    s.oneWire.count = 5;
    s.sensors.count = 6;
    dpg_setSensor(s, 0, true, false, SensorState::Ok);
    dpg_setSensor(s, 1, true, true, SensorState::Fault);
    dpg_setSensor(s, 2, true, false, SensorState::Fault);
    dpg_setSensor(s, 3, false, false, SensorState::Unassigned);
    dpg_setSensor(s, 4, true, false, SensorState::Unknown);
    dpg_setSensor(s, 5, true, false, SensorState::Ok);
    DisplayLabels labels = dpg_labels();
    DisplayFrame f{};
    renderSensorsPage(s, f, &labels);
    TEST_ASSERT_EQUAL_STRING("SENSORS", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Bus 5 Asg 5 Miss 1", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("T1 OK", dpg_body(f, 1, 0));
    TEST_ASSERT_EQUAL_STRING("T2 MISS", dpg_body(f, 1, 66));
    TEST_ASSERT_EQUAL_STRING("T3 FAULT", dpg_body(f, 2, 0));
    TEST_ASSERT_EQUAL_STRING("T4 --", dpg_body(f, 2, 66));
    TEST_ASSERT_EQUAL_STRING("T5 WAIT", dpg_body(f, 3, 0));
    TEST_ASSERT_EQUAL_STRING("T6 OK", dpg_body(f, 3, 66));
    TEST_ASSERT_NULL(dpg_body(f, 4, 0));
    // Status only: no temperature is ever printed (the unassigned one has tempC set).
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        TEST_ASSERT_NULL(strstr(f.line[i].text, "45"));
    }
}

static void dpg_test_sensors_none_overflow_scan() {
    CommonState s{};
    DisplayLabels labels = dpg_labels();
    DisplayFrame f{};
    renderSensorsPage(s, f, &labels);  // scan not done yet, no logical sensors
    TEST_ASSERT_EQUAL_STRING("Bus -- Asg 0 Miss 0", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("No sensors", dpg_body(f, 1));

    s.oneWire.done = true;
    s.oneWire.scanCount = 3;
    s.oneWire.count = 12;
    s.oneWire.overflow = true;
    renderSensorsPage(s, f, &labels);
    TEST_ASSERT_EQUAL_STRING("Bus 12+ Asg 0 Miss 0", dpg_body(f, 0));

    // Labels with 0 sensors cap the list too.
    s.sensors.count = 2;
    DisplayLabels none = dpg_labels(0);
    renderSensorsPage(s, f, &none);
    TEST_ASSERT_EQUAL_STRING("No sensors", dpg_body(f, 1));
}

static void dpg_test_sensors_null_ctx_names() {
    CommonState s{};
    s.oneWire.done = true;
    s.sensors.count = 3;
    dpg_setSensor(s, 0, true, false, SensorState::Ok);
    dpg_setSensor(s, 1, false, false, SensorState::Unassigned);
    dpg_setSensor(s, 2, true, false, SensorState::Ok);
    DisplayFrame f{};
    renderSensorsPage(s, f, nullptr);
    TEST_ASSERT_EQUAL_STRING("Bus 0 Asg 2 Miss 0", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("S1 OK", dpg_body(f, 1, 0));
    TEST_ASSERT_EQUAL_STRING("S2 --", dpg_body(f, 1, 66));
    TEST_ASSERT_EQUAL_STRING("S3 OK", dpg_body(f, 2, 0));
}

// ---- Alarms ----

static void dpg_test_alarms_single_page() {
    CommonState s{};
    s.alarms.activeMask = (1u << 0) | (1u << 5) | (1u << 24);
    DisplayLabels labels = dpg_labels();
    labels.alarms = DPG_ALARMS;
    labels.alarmCount = 1;
    DisplayFrame f{};
    renderAlarmsPage(s, 0, labels, f);
    TEST_ASSERT_EQUAL_STRING("ALARMS 3", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Boiler overheat", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("Alarm 5", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("Sensor T1 missing", dpg_body(f, 2));  // HW sensor name
    TEST_ASSERT_NULL(dpg_body(f, 3));
}

static void dpg_test_alarms_sub_pages() {
    CommonState s{};
    s.alarms.activeMask = 0x7Fu;  // bits 0..6
    const DisplayLabels labels = dpg_labels();
    DisplayFrame f{};
    renderAlarmsPage(s, 0, labels, f);
    TEST_ASSERT_EQUAL_STRING("ALARMS 7 1/2", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Alarm 0", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("Alarm 4", dpg_body(f, 4));
    TEST_ASSERT_EQUAL_UINT8(6, f.lineCount);

    renderAlarmsPage(s, 1, labels, f);
    TEST_ASSERT_EQUAL_STRING("ALARMS 7 2/2", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Alarm 5", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("Alarm 6", dpg_body(f, 1));
    TEST_ASSERT_NULL(dpg_body(f, 2));
    TEST_ASSERT_EQUAL_UINT8(3, f.lineCount);

    renderAlarmsPage(s, 2, labels, f);  // wraps modulo the sub-page count
    TEST_ASSERT_EQUAL_STRING("ALARMS 7 1/2", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Alarm 0", dpg_body(f, 0));
}

static void dpg_test_alarms_sensor_bits_and_empty() {
    CommonState s{};
    s.alarms.activeMask = (1u << 25) | (1u << 31);
    const DisplayLabels labels = dpg_labels();
    DisplayFrame f{};
    renderAlarmsPage(s, 0, labels, f);
    TEST_ASSERT_EQUAL_STRING("ALARMS 2", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Sensor T2 missing", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("Sensor T8 missing", dpg_body(f, 1));

    s.alarms.activeMask = 0;
    renderAlarmsPage(s, 0, labels, f);
    TEST_ASSERT_EQUAL_STRING("ALARMS", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("No active alarms", dpg_body(f, 0));
}

// ---- Special screens ----

static void dpg_test_setup_screen() {
    NetworkStatus n{};
    strcpy(n.apSsid, "Heater-Setup-1A2B");
    strcpy(n.apIp, "192.168.4.1");
    DisplayFrame f{};
    renderSetupScreen(n, f);
    TEST_ASSERT_EQUAL_UINT8(4, f.lineCount);
    TEST_ASSERT_EQUAL_STRING("SETUP MODE", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Heater-Setup-1A2B", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("(open, no password)", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("Open: 192.168.4.1", dpg_body(f, 2));

    NetworkStatus empty{};
    strcpy(empty.apSsid, "");
    renderSetupScreen(empty, f);
    TEST_ASSERT_EQUAL_STRING("--", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("Open: 192.168.4.1", dpg_body(f, 2));  // apIp fallback

    strcpy(n.apIp, "10.1.2.3");
    renderSetupScreen(n, f);
    TEST_ASSERT_EQUAL_STRING("Open: 10.1.2.3", dpg_body(f, 2));
}

static void dpg_test_reset_countdown() {
    DisplayFrame f{};
    renderResetScreen(ResetGatePhase::Countdown, 7, f);
    TEST_ASSERT_EQUAL_STRING("FACTORY RESET", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("DI1 held", dpg_body(f, 0));
    const char* digits = nullptr;
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        if (f.line[i].font == DisplayFont::Large) {
            TEST_ASSERT_EQUAL_UINT8(44, f.line[i].baseline);
            TEST_ASSERT_EQUAL_UINT8((126 - 3 * 10) / 2, f.line[i].x);  // centered
            digits = f.line[i].text;
        }
    }
    TEST_ASSERT_EQUAL_STRING("7 s", digits);
    TEST_ASSERT_TRUE(f.bar.used);
    TEST_ASSERT_EQUAL_UINT8(0, f.bar.x);
    TEST_ASSERT_EQUAL_UINT8(50, f.bar.y);
    TEST_ASSERT_EQUAL_UINT8(126, f.bar.w);
    TEST_ASSERT_EQUAL_UINT8(6, f.bar.h);
    TEST_ASSERT_EQUAL_UINT8(30, f.bar.percent);
    TEST_ASSERT_NULL(dpg_body(f, 4));  // the bar sits there

    renderResetScreen(ResetGatePhase::Countdown, 10, f);
    TEST_ASSERT_EQUAL_UINT8(0, f.bar.percent);
    renderResetScreen(ResetGatePhase::Countdown, 15, f);  // min(n, 10)
    TEST_ASSERT_EQUAL_UINT8(0, f.bar.percent);
    renderResetScreen(ResetGatePhase::Countdown, 0, f);
    TEST_ASSERT_EQUAL_UINT8(100, f.bar.percent);
}

static void dpg_test_reset_results() {
    DisplayFrame f{};
    renderResetScreen(ResetGatePhase::Confirmed, 0, f);
    TEST_ASSERT_EQUAL_STRING("FACTORY RESET", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Reset confirmed", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("Settings erased", dpg_body(f, 2));
    TEST_ASSERT_FALSE(f.bar.used);

    renderResetScreen(ResetGatePhase::Aborted, 0, f);
    TEST_ASSERT_EQUAL_STRING("Reset aborted", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("Settings kept", dpg_body(f, 2));

    renderResetScreen(ResetGatePhase::Inactive, 0, f);
    TEST_ASSERT_EQUAL_UINT8(0, f.lineCount);
    TEST_ASSERT_FALSE(f.titleRule);
    TEST_ASSERT_FALSE(f.bar.used);
}

static void dpg_test_ota_and_splash() {
    OtaStatus o{};
    o.inProgress = true;
    o.source = OtaSource::Web;
    DisplayFrame f{};
    renderOtaScreen(o, f);
    TEST_ASSERT_EQUAL_STRING("UPDATING", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("Firmware update...", dpg_body(f, 0));
    TEST_ASSERT_EQUAL_STRING("Do not power off", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("via web", dpg_body(f, 2));
    o.source = OtaSource::Espota;
    renderOtaScreen(o, f);
    TEST_ASSERT_EQUAL_STRING("via espota", dpg_body(f, 2));
    o.source = OtaSource::None;
    renderOtaScreen(o, f);
    TEST_ASSERT_NULL(dpg_body(f, 2));
    TEST_ASSERT_EQUAL_UINT8(3, f.lineCount);

    renderBootSplash("boiler-room", "1.2.3", f);
    TEST_ASSERT_EQUAL_STRING("boiler-room", dpg_title(f));
    TEST_ASSERT_EQUAL_STRING("fw 1.2.3", dpg_body(f, 1));
    TEST_ASSERT_EQUAL_STRING("starting...", dpg_body(f, 3));

    renderBootSplash("x", "1.2.3-dev+abcdef0123456789abcdef", f);  // long version fitted
    TEST_ASSERT_EQUAL_UINT32(21, strlen(dpg_body(f, 1)));
    TEST_ASSERT_EQUAL('~', dpg_body(f, 1)[20]);
}

// ---- Layout invariant (every renderer) ----

static void dpg_test_layout_invariant() {
    CommonState s{};
    dpg_connected(s);
    strcpy(s.network.ssid, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    strcpy(s.network.ip, "255.255.255.255");
    strcpy(s.network.apSsid, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    strcpy(s.network.apIp, "255.255.255.255");
    s.network.wifiRssi = -128;
    s.oneWire.done = true;
    s.oneWire.count = 12;
    s.oneWire.overflow = true;
    s.sensors.count = MAX_LOGICAL_SENSORS;
    for (uint8_t i = 0; i < MAX_LOGICAL_SENSORS; ++i) {
        dpg_setSensor(s, i, true, false, SensorState::Fault);
    }
    static const LogicalSensorDesc longNames[MAX_LOGICAL_SENSORS] = {
        {"LongSensorName1", "k"}, {"LongSensorName2", "k"}, {"LongSensorName3", "k"}, {"LongSensorName4", "k"},
        {"LongSensorName5", "k"}, {"LongSensorName6", "k"}, {"LongSensorName7", "k"}, {"LongSensorName8", "k"},
    };
    DisplayLabels labels = dpg_labels();
    labels.sensors = longNames;
    DisplayFrame f{};

    renderNetworkPage(s, f, nullptr);
    dpg_assertLayout(f);

    renderSensorsPage(s, f, &labels);
    dpg_assertLayout(f);
    TEST_ASSERT_EQUAL_UINT8(10, f.lineCount);  // title + summary + 8 cells
    // Column 0 never runs into column 1.
    for (uint8_t i = 0; i < f.lineCount; ++i) {
        if (f.line[i].x == 0 && f.line[i].baseline > DISPLAY_BODY_BASELINE[0]) {
            TEST_ASSERT_TRUE(strlen(f.line[i].text) * 6 < 66);
        }
    }

    s.alarms.activeMask = 0xFFFFFFFFu;
    for (uint8_t p = 0; p < 7; ++p) {
        renderAlarmsPage(s, p, labels, f);
        dpg_assertLayout(f);
    }
    s.alarms.activeMask = 0;
    renderAlarmsPage(s, 0, labels, f);
    dpg_assertLayout(f);

    renderSetupScreen(s.network, f);
    dpg_assertLayout(f);

    static const uint8_t DPG_RESET_SECONDS[] = {0, 7, 10, 255};
    const ResetGatePhase phases[] = {ResetGatePhase::Countdown, ResetGatePhase::Confirmed, ResetGatePhase::Aborted,
                                     ResetGatePhase::Inactive};
    for (ResetGatePhase ph : phases) {
        for (uint8_t n : DPG_RESET_SECONDS) {
            renderResetScreen(ph, n, f);
            dpg_assertLayout(f);
        }
    }
    renderResetScreen(ResetGatePhase::Countdown, 255, f);
    TEST_ASSERT_EQUAL_UINT8(3, f.lineCount);  // "255 s" still fits in Large

    OtaStatus o{};
    o.source = OtaSource::Espota;
    renderOtaScreen(o, f);
    dpg_assertLayout(f);

    renderBootSplash("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", "1.2.3-dev+abcdef0123456789abcdef", f);
    dpg_assertLayout(f);
}

inline void runDisplayPagesSuite() {
    RUN_TEST(dpg_test_list_capacity);
    RUN_TEST(dpg_test_list_rejects_null_render);
    RUN_TEST(dpg_test_append_shared_order);
    RUN_TEST(dpg_test_append_shared_no_room);
    RUN_TEST(dpg_test_subpage_count);
    RUN_TEST(dpg_test_network_connected);
    RUN_TEST(dpg_test_network_connecting);
    RUN_TEST(dpg_test_network_setup_ap_mqtt_disabled);
    RUN_TEST(dpg_test_network_long_ssid_and_ip);
    RUN_TEST(dpg_test_sensors_mixed);
    RUN_TEST(dpg_test_sensors_none_overflow_scan);
    RUN_TEST(dpg_test_sensors_null_ctx_names);
    RUN_TEST(dpg_test_alarms_single_page);
    RUN_TEST(dpg_test_alarms_sub_pages);
    RUN_TEST(dpg_test_alarms_sensor_bits_and_empty);
    RUN_TEST(dpg_test_setup_screen);
    RUN_TEST(dpg_test_reset_countdown);
    RUN_TEST(dpg_test_reset_results);
    RUN_TEST(dpg_test_ota_and_splash);
    RUN_TEST(dpg_test_layout_invariant);
}
