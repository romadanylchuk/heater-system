#pragma once
#include <unity.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "../lib/CoreEngine/src/CommonState.h"
#include "../lib/CoreEngine/src/Descriptors.h"
#include "../lib/CoreEngine/src/KvStore.h"
#include "../lib/HwEngine/src/HwConfig.h"
#include "../lib/DisplayEngine/src/DisplayFormat.h"
#include "../lib/DisplayEngine/src/DisplayFrame.h"
#include "../lib/DisplayEngine/src/DisplaySettings.h"
#include "../lib/DisplayEngine/src/PixelShift.h"
#include "../lib/DisplayEngine/src/TileScheduler.h"

// Native-safe Unity tests for the DisplayEngine leaf modules (stage 06
// phase 1): frame model / text fitting, formatting, pixel shift, tile
// scheduler and display settings. Header-only, run via DisplaySuite.h.
// Test names are prefixed disp_.

// ---- DisplayFrame ----

static void disp_test_fit_chars() {
    TEST_ASSERT_EQUAL_UINT32(21, displayFitChars(DisplayFont::Normal, 0));
    TEST_ASSERT_EQUAL_UINT32(25, displayFitChars(DisplayFont::Small, 0));
    TEST_ASSERT_EQUAL_UINT32(12, displayFitChars(DisplayFont::Large, 0));
    TEST_ASSERT_EQUAL_UINT32(0, displayFitChars(DisplayFont::Normal, 126));
    TEST_ASSERT_EQUAL_UINT32(0, displayFitChars(DisplayFont::Small, 200));
}

static void disp_test_fit_text_truncates_with_tilde() {
    char out[DISPLAY_TEXT_MAX + 1];
    displayFitText("abcdefghijklmnopqrstuvwxyz", DisplayFont::Normal, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrst~", out);
    TEST_ASSERT_EQUAL_UINT32(21, strlen(out));

    displayFitText("short", DisplayFont::Normal, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("short", out);

    // Exactly fitting text is not truncated.
    displayFitText("123456789012", DisplayFont::Large, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("123456789012", out);

    // cap limits too, and the result stays NUL-terminated.
    char small[4];
    displayFitText("abcdef", DisplayFont::Normal, 0, small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("ab~", small);
}

static void disp_test_fit_text_non_ascii_and_null() {
    char out[DISPLAY_TEXT_MAX + 1];
    displayFitText("Wi\xC3\xA9" "Fi\x01", DisplayFont::Normal, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Wi??Fi?", out);

    strcpy(out, "junk");
    displayFitText(nullptr, DisplayFont::Normal, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void disp_test_add_text_clip_rules() {
    DisplayFrame f;
    f.clear();
    // Normal descent 2: baseline 60 -> bottom row 62 > 61.
    TEST_ASSERT_FALSE(f.addText(DisplayFont::Normal, 0, 60, "x"));
    // Normal ascent 8: baseline 7 would clip at the top.
    TEST_ASSERT_FALSE(f.addText(DisplayFont::Normal, 0, 7, "x"));
    // x + advance beyond the content width.
    TEST_ASSERT_FALSE(f.addText(DisplayFont::Normal, 121, 19, "x"));
    TEST_ASSERT_FALSE(f.addText(DisplayFont::Normal, 0, 19, nullptr));
    TEST_ASSERT_EQUAL_UINT8(0, f.lineCount);

    TEST_ASSERT_TRUE(f.setTitle("TITLE"));
    TEST_ASSERT_TRUE(f.titleRule);
    for (uint8_t row = 0; row < DISPLAY_BODY_ROWS; ++row) {
        TEST_ASSERT_TRUE(f.addBody(row, "body"));
        TEST_ASSERT_EQUAL_UINT8(DISPLAY_BODY_BASELINE[row], f.line[row + 1].baseline);
    }
    TEST_ASSERT_FALSE(f.addBody(5, "nope"));
    TEST_ASSERT_EQUAL_UINT8(6, f.lineCount);
    TEST_ASSERT_EQUAL_UINT8(DISPLAY_TITLE_BASELINE, f.line[0].baseline);
    TEST_ASSERT_EQUAL_STRING("TITLE", f.line[0].text);

    // Large at the lowest legal baseline (61 - 4 = 57) fits; 58 does not.
    TEST_ASSERT_TRUE(f.addText(DisplayFont::Large, 0, 57, "L"));
    TEST_ASSERT_FALSE(f.addText(DisplayFont::Large, 0, 58, "L"));
}

static void disp_test_add_text_truncates_at_x() {
    DisplayFrame f;
    f.clear();
    TEST_ASSERT_TRUE(f.addBody(0, "0123456789abcdef", 60));  // (126-60)/6 = 11 chars
    TEST_ASSERT_EQUAL_STRING("0123456789~", f.line[0].text);
    TEST_ASSERT_EQUAL_UINT8(60, f.line[0].x);
}

static void disp_test_frame_full() {
    DisplayFrame f;
    f.clear();
    for (size_t i = 0; i < DISPLAY_MAX_LINES; ++i) {
        TEST_ASSERT_TRUE(f.addText(DisplayFont::Small, 0, 20, "x"));
    }
    TEST_ASSERT_FALSE(f.addText(DisplayFont::Small, 0, 20, "x"));
    TEST_ASSERT_EQUAL_UINT8(DISPLAY_MAX_LINES, f.lineCount);

    f.setTitle(nullptr);
    f.setBar(0, 0, 10, 10, 50);
    f.clear();
    TEST_ASSERT_EQUAL_UINT8(0, f.lineCount);
    TEST_ASSERT_FALSE(f.titleRule);
    TEST_ASSERT_FALSE(f.bar.used);
}

static void disp_test_add_centered() {
    DisplayFrame f;
    f.clear();
    TEST_ASSERT_TRUE(f.addCentered(DisplayFont::Normal, 30, "abcd"));  // (126 - 24) / 2
    TEST_ASSERT_EQUAL_UINT8(51, f.line[0].x);
    TEST_ASSERT_TRUE(f.addCentered(DisplayFont::Large, 40, "12345"));  // (126 - 50) / 2
    TEST_ASSERT_EQUAL_UINT8(38, f.line[1].x);
    TEST_ASSERT_TRUE(f.addCentered(DisplayFont::Normal, 50, "abcdefghijklmnopqrstuvwxyz"));
    TEST_ASSERT_EQUAL_UINT8(0, f.line[2].x);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrst~", f.line[2].text);
    TEST_ASSERT_FALSE(f.addCentered(DisplayFont::Normal, 60, "clip"));
}

static void disp_test_set_bar_clamps() {
    DisplayFrame f;
    f.clear();
    f.setBar(10, 40, 200, 50, 150);
    TEST_ASSERT_TRUE(f.bar.used);
    TEST_ASSERT_EQUAL_UINT8(10, f.bar.x);
    TEST_ASSERT_EQUAL_UINT8(40, f.bar.y);
    TEST_ASSERT_EQUAL_UINT8(116, f.bar.w);   // 10 + 116 = 126
    TEST_ASSERT_EQUAL_UINT8(22, f.bar.h);    // 40 + 22 = 62
    TEST_ASSERT_EQUAL_UINT8(100, f.bar.percent);

    f.setBar(4, 50, 100, 8, 42);
    TEST_ASSERT_EQUAL_UINT8(100, f.bar.w);
    TEST_ASSERT_EQUAL_UINT8(8, f.bar.h);
    TEST_ASSERT_EQUAL_UINT8(42, f.bar.percent);

    f.setBar(250, 250, 5, 5, 0);
    TEST_ASSERT_TRUE(f.bar.x + f.bar.w <= DISPLAY_CONTENT_WIDTH);
    TEST_ASSERT_TRUE(f.bar.y + f.bar.h <= DISPLAY_CONTENT_HEIGHT);
}

// ---- DisplayFormat ----

static void disp_test_wifi_state_text() {
    NetworkStatus n{};
    n.wifiConnected = true;
    n.setupApActive = true;
    TEST_ASSERT_EQUAL_STRING("connected", wifiStateText(n));
    n.wifiConnected = false;
    TEST_ASSERT_EQUAL_STRING("setup AP", wifiStateText(n));
    n.setupApActive = false;
    n.wifiMode = NetWifiMode::Station;
    TEST_ASSERT_EQUAL_STRING("connecting", wifiStateText(n));
    n.wifiMode = NetWifiMode::Off;
    TEST_ASSERT_EQUAL_STRING("disconnected", wifiStateText(n));
}

static void disp_test_mqtt_state_text() {
    NetworkStatus n{};
    n.mqttConnected = true;
    n.mqttEnabled = false;
    TEST_ASSERT_EQUAL_STRING("disabled", mqttStateText(n));
    n.mqttEnabled = true;
    TEST_ASSERT_EQUAL_STRING("connected", mqttStateText(n));
    n.mqttConnected = false;
    TEST_ASSERT_EQUAL_STRING("disconnected", mqttStateText(n));
}

static void disp_test_format_rssi() {
    NetworkStatus n{};
    n.wifiRssi = -67;
    char out[16];
    strcpy(out, "junk");
    TEST_ASSERT_FALSE(formatRssi(n, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    n.wifiConnected = true;
    TEST_ASSERT_TRUE(formatRssi(n, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("-67 dBm", out);
}

static void disp_test_format_temp() {
    char out[12];
    formatTempC(45.2f, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("45.2", out);
    formatTempC(-3.5f, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("-3.5", out);
    formatTempC(85.0f, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("85.0", out);
    formatTempC(45.2f, false, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("--", out);
    formatTempC(NAN, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("--", out);
    formatTempC(130.0f, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("--", out);
    formatTempC(-60.0f, true, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("--", out);
}

static void disp_test_sensor_state_text() {
    LogicalSensorStatus s{};
    s.state = SensorState::Ok;
    TEST_ASSERT_EQUAL_STRING("--", sensorStateText(s));
    s.assigned = true;
    TEST_ASSERT_EQUAL_STRING("OK", sensorStateText(s));
    s.missing = true;
    TEST_ASSERT_EQUAL_STRING("MISS", sensorStateText(s));
    s.missing = false;
    s.state = SensorState::Fault;
    TEST_ASSERT_EQUAL_STRING("FAULT", sensorStateText(s));
    s.state = SensorState::Unknown;
    TEST_ASSERT_EQUAL_STRING("WAIT", sensorStateText(s));
}

static void disp_test_alarm_label() {
    static const LogicalSensorDesc sensors[] = {
        {"T1", "sensorT1"}, {"T2", "sensorT2"}, {"T3", "sensorT3"}};
    static const AlarmDescriptor alarms[] = {
        {"overheat", "Overheat", "Перегрів"}, {"k1", nullptr, nullptr}};
    char out[32];
    DisplayLabels labels{alarms, 2, sensors, 3};

    alarmLabel(26, labels, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Sensor T3 missing", out);
    alarmLabel(30, labels, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Alarm 30", out);
    alarmLabel(0, labels, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Overheat", out);
    alarmLabel(1, labels, out, sizeof(out));   // null labelEn
    TEST_ASSERT_EQUAL_STRING("Alarm 1", out);
    alarmLabel(2, labels, out, sizeof(out));   // beyond alarmCount
    TEST_ASSERT_EQUAL_STRING("Alarm 2", out);

    DisplayLabels none{nullptr, 0, nullptr, 0};
    alarmLabel(5, none, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Alarm 5", out);
    alarmLabel(24, none, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Alarm 24", out);

    char tiny[7];
    memset(tiny, 'x', sizeof(tiny));
    alarmLabel(24, labels, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_STRING("Sensor", tiny);
    TEST_ASSERT_EQUAL_CHAR('\0', tiny[sizeof(tiny) - 1]);
}

static void disp_test_alarm_count() {
    TEST_ASSERT_EQUAL_UINT8(0, alarmCount(0));
    TEST_ASSERT_EQUAL_UINT8(1, alarmCount(0x80000000u));
    TEST_ASSERT_EQUAL_UINT8(3, alarmCount(0x01000101u));
    TEST_ASSERT_EQUAL_UINT8(32, alarmCount(0xFFFFFFFFu));
}

// ---- PixelShift ----

static void disp_test_pixel_shift_ring() {
    for (uint32_t step = 0; step < PIXEL_SHIFT_STEPS; ++step) {
        const PixelOffset a = pixelShiftOffset(step);
        const PixelOffset b = pixelShiftOffset(step + 1);  // includes 7 -> 0
        TEST_ASSERT_TRUE(a.dx <= DISPLAY_SHIFT_MAX);
        TEST_ASSERT_TRUE(a.dy <= DISPLAY_SHIFT_MAX);
        const int ddx = static_cast<int>(a.dx) - static_cast<int>(b.dx);
        const int ddy = static_cast<int>(a.dy) - static_cast<int>(b.dy);
        const int manhattan = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
        TEST_ASSERT_EQUAL_INT(1, manhattan);
    }
    const PixelOffset s0 = pixelShiftOffset(0);
    const PixelOffset s8 = pixelShiftOffset(8);
    TEST_ASSERT_EQUAL_UINT8(0, s0.dx);
    TEST_ASSERT_EQUAL_UINT8(0, s0.dy);
    TEST_ASSERT_EQUAL_UINT8(s0.dx, s8.dx);
    TEST_ASSERT_EQUAL_UINT8(s0.dy, s8.dy);
    const PixelOffset s4 = pixelShiftOffset(4);
    TEST_ASSERT_EQUAL_UINT8(2, s4.dx);
    TEST_ASSERT_EQUAL_UINT8(2, s4.dy);
}

// ---- TileScheduler ----

static void disp_test_tile_scheduler() {
    static TileScheduler ts;
    static uint8_t frame[DISPLAY_BUFFER_BYTES];
    ts = TileScheduler();
    memset(frame, 0, sizeof(frame));

    TEST_ASSERT_TRUE(ts.hasPending());
    TEST_ASSERT_EQUAL_UINT8(0xFF, ts.pendingMask());
    TEST_ASSERT_EQUAL_INT8(0, ts.nextRow());

    for (uint8_t row = 0; row < DISPLAY_TILE_ROWS; ++row) {
        ts.markSent(row, frame);
    }
    TEST_ASSERT_FALSE(ts.hasPending());
    TEST_ASSERT_EQUAL_INT8(-1, ts.nextRow());

    TEST_ASSERT_EQUAL_UINT8(0x00, ts.scan(frame));  // identical frame adds nothing

    frame[5 * DISPLAY_ROW_BYTES + 17] = 0x42;
    TEST_ASSERT_EQUAL_UINT8(0x20, ts.scan(frame));
    TEST_ASSERT_EQUAL_INT8(5, ts.nextRow());

    frame[2 * DISPLAY_ROW_BYTES + 127] = 0x01;
    TEST_ASSERT_EQUAL_UINT8(0x24, ts.scan(frame));
    TEST_ASSERT_EQUAL_INT8(2, ts.nextRow());

    ts.markSent(2, frame);
    TEST_ASSERT_EQUAL_UINT8(0x20, ts.pendingMask());
    ts.markSent(8, frame);  // out of range: ignored
    TEST_ASSERT_EQUAL_UINT8(0x20, ts.pendingMask());
    ts.markSent(5, frame);
    TEST_ASSERT_FALSE(ts.hasPending());
    TEST_ASSERT_EQUAL_UINT8(0x00, ts.scan(frame));

    ts.invalidateAll();
    TEST_ASSERT_EQUAL_UINT8(0xFF, ts.pendingMask());
}

static void disp_test_rows_for_pass() {
    TEST_ASSERT_EQUAL_UINT8(2, displayRowsForPass(0));
    TEST_ASSERT_EQUAL_UINT8(2, displayRowsForPass(39));
    TEST_ASSERT_EQUAL_UINT8(1, displayRowsForPass(40));
    TEST_ASSERT_EQUAL_UINT8(1, displayRowsForPass(79));
    TEST_ASSERT_EQUAL_UINT8(0, displayRowsForPass(80));
    TEST_ASSERT_EQUAL_UINT8(0, displayRowsForPass(1000));
}

// ---- DisplaySettings ----

static_assert(displayContrastFromPercent(30) == 77, "default brightness maps to contrast 77");

static void disp_test_contrast_mapping() {
    TEST_ASSERT_EQUAL_UINT8(3, displayContrastFromPercent(0));
    TEST_ASSERT_EQUAL_UINT8(3, displayContrastFromPercent(1));
    TEST_ASSERT_EQUAL_UINT8(77, displayContrastFromPercent(30));
    TEST_ASSERT_EQUAL_UINT8(255, displayContrastFromPercent(100));
    TEST_ASSERT_EQUAL_UINT8(255, displayContrastFromPercent(150));
}

static void disp_test_rotate_period() {
    TEST_ASSERT_EQUAL_UINT32(2000, displayRotatePeriodMs(0));
    TEST_ASSERT_EQUAL_UINT32(5000, displayRotatePeriodMs(5));
    TEST_ASSERT_EQUAL_UINT32(60000, displayRotatePeriodMs(999));
}

static void disp_test_settings_table() {
    TEST_ASSERT_EQUAL_UINT32(DISPLAY_SETTING_COUNT, DISPLAY_SETTINGS_TABLE.count);
    for (size_t i = 0; i < DISPLAY_SETTINGS_TABLE.count; ++i) {
        const SettingDescriptor& d = DISPLAY_SETTINGS_TABLE.items[i];
        TEST_ASSERT_TRUE(strlen(d.nvsKey) <= NVS_KEY_MAX_LEN);
        TEST_ASSERT_TRUE(strlen(d.key) <= SETTING_KEY_MAX_LEN);
        TEST_ASSERT_TRUE(d.type == SettingType::Int);
        TEST_ASSERT_TRUE(d.defaultValue >= d.minValue);
        TEST_ASSERT_TRUE(d.defaultValue <= d.maxValue);
        TEST_ASSERT_EQUAL_STRING("display", d.group);
        TEST_ASSERT_EQUAL_UINT8(0, d.flags & SETTING_FLAG_NO_HA);
    }
    TEST_ASSERT_EQUAL_STRING(DISPLAY_KEY_ROTATE_S, DISPLAY_SETTINGS[0].key);
    TEST_ASSERT_EQUAL_STRING(DISPLAY_KEY_BRIGHTNESS, DISPLAY_SETTINGS[1].key);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, DISPLAY_SETTINGS[0].defaultValue);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, DISPLAY_SETTINGS[1].defaultValue);
}

// Runs every test in this suite. Called from DisplaySuite.h.
inline void runDisplayFrameSuite() {
    RUN_TEST(disp_test_fit_chars);
    RUN_TEST(disp_test_fit_text_truncates_with_tilde);
    RUN_TEST(disp_test_fit_text_non_ascii_and_null);
    RUN_TEST(disp_test_add_text_clip_rules);
    RUN_TEST(disp_test_add_text_truncates_at_x);
    RUN_TEST(disp_test_frame_full);
    RUN_TEST(disp_test_add_centered);
    RUN_TEST(disp_test_set_bar_clamps);
    RUN_TEST(disp_test_wifi_state_text);
    RUN_TEST(disp_test_mqtt_state_text);
    RUN_TEST(disp_test_format_rssi);
    RUN_TEST(disp_test_format_temp);
    RUN_TEST(disp_test_sensor_state_text);
    RUN_TEST(disp_test_alarm_label);
    RUN_TEST(disp_test_alarm_count);
    RUN_TEST(disp_test_pixel_shift_ring);
    RUN_TEST(disp_test_tile_scheduler);
    RUN_TEST(disp_test_rows_for_pass);
    RUN_TEST(disp_test_contrast_mapping);
    RUN_TEST(disp_test_rotate_period);
    RUN_TEST(disp_test_settings_table);
}
