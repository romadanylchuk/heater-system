#include "DisplayPages.h"
#include <stdio.h>
#include <string.h>

static constexpr uint8_t SENSOR_COLUMN_X = 66;
static constexpr size_t SENSOR_COLUMN_CHARS = 10;  // col 0 must end before x = 66 (60 px + gap)
static constexpr uint8_t RESET_DIGITS_BASELINE = 44;
static constexpr uint8_t RESET_BAR_X = 0, RESET_BAR_Y = 50, RESET_BAR_W = 126, RESET_BAR_H = 6;
static constexpr uint8_t RESET_COUNTDOWN_S = 10;
static constexpr const char* SETUP_DEFAULT_AP_IP = "192.168.4.1";

// ---- DisplayPageList ----

bool DisplayPageList::add(const DisplayPageDesc& page) {
    if (page.render == nullptr || _count >= DISPLAY_MAX_PAGES) {
        return false;
    }
    _pages[_count] = page;
    ++_count;
    return true;
}

size_t DisplayPageList::count() const {
    return _count;
}

const DisplayPageDesc* DisplayPageList::at(size_t i) const {
    return i < _count ? &_pages[i] : nullptr;
}

bool appendSharedPages(DisplayPageList& list, const DisplayLabels* labels) {
    if (list.count() + 2 > DISPLAY_MAX_PAGES) {
        return false;  // all or nothing: never register Network without Sensors
    }
    const DisplayPageDesc network = {"Network", renderNetworkPage, nullptr};
    const DisplayPageDesc sensors = {"Sensors", renderSensorsPage, const_cast<DisplayLabels*>(labels)};
    return list.add(network) && list.add(sensors);
}

uint8_t alarmSubPageCount(uint32_t mask) {
    const uint8_t n = alarmCount(mask);
    return static_cast<uint8_t>((n + DISPLAY_ALARMS_PER_SUBPAGE - 1) / DISPLAY_ALARMS_PER_SUBPAGE);
}

// ---- Network ----

void renderNetworkPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    (void)ctx;
    const NetworkStatus& n = s.network;
    char buf[64];
    f.clear();
    f.setTitle("NETWORK");
    snprintf(buf, sizeof(buf), "WiFi: %s", wifiStateText(n));
    f.addBody(0, buf);
    if (n.ssid[0] != '\0') {
        snprintf(buf, sizeof(buf), "SSID: %s", n.ssid);
        f.addBody(1, buf);
    }
    char rssi[16];
    if (formatRssi(n, rssi, sizeof(rssi))) {
        snprintf(buf, sizeof(buf), "RSSI: %s", rssi);
        f.addBody(2, buf);
    }
    snprintf(buf, sizeof(buf), "IP: %s", (n.wifiConnected && n.ip[0] != '\0') ? n.ip : "--");
    f.addBody(3, buf);
    snprintf(buf, sizeof(buf), "MQTT: %s", mqttStateText(n));
    f.addBody(4, buf);
}

// ---- Sensors ----

// Copies text into out keeping at most maxChars; marks a cut with '~'.
static void fitColumn(const char* in, char* out, size_t maxChars) {
    size_t n = 0;
    while (n < maxChars && in[n] != '\0') {
        out[n] = in[n];
        ++n;
    }
    out[n] = '\0';
    if (in[n] != '\0' && n > 0) {
        out[n - 1] = '~';
    }
}

void renderSensorsPage(const CommonState& s, DisplayFrame& f, void* ctx) {
    const DisplayLabels* labels = static_cast<const DisplayLabels*>(ctx);
    char buf[48];
    f.clear();
    f.setTitle("SENSORS");

    size_t logical = s.sensors.count;
    if (logical > MAX_LOGICAL_SENSORS) {
        logical = MAX_LOGICAL_SENSORS;
    }
    unsigned assigned = 0, missing = 0;
    for (size_t i = 0; i < logical; ++i) {
        assigned += s.sensors.sensor[i].assigned ? 1u : 0u;
        missing += s.sensors.sensor[i].missing ? 1u : 0u;
    }
    char bus[8];
    if (!s.oneWire.done && s.oneWire.scanCount == 0) {
        snprintf(bus, sizeof(bus), "--");
    } else {
        snprintf(bus, sizeof(bus), "%u%s", static_cast<unsigned>(s.oneWire.count), s.oneWire.overflow ? "+" : "");
    }
    snprintf(buf, sizeof(buf), "Bus %s Asg %u Miss %u", bus, assigned, missing);
    f.addBody(0, buf);

    size_t shown = logical;
    if (labels != nullptr && labels->sensorCount < shown) {
        shown = labels->sensorCount;
    }
    if (shown == 0) {
        f.addBody(1, "No sensors");
        return;
    }
    for (size_t i = 0; i < shown; ++i) {
        const char* name = (labels != nullptr && labels->sensors != nullptr) ? labels->sensors[i].name : nullptr;
        char fallback[8];
        if (name == nullptr) {
            snprintf(fallback, sizeof(fallback), "S%u", static_cast<unsigned>(i + 1));
            name = fallback;
        }
        snprintf(buf, sizeof(buf), "%s %s", name, sensorStateText(s.sensors.sensor[i]));
        char cell[SENSOR_COLUMN_CHARS + 1];
        fitColumn(buf, cell, SENSOR_COLUMN_CHARS);
        const uint8_t x = (i % 2 == 0) ? 0 : SENSOR_COLUMN_X;
        f.addBody(static_cast<uint8_t>(1 + i / 2), cell, x);
    }
}

// ---- Alarms ----

void renderAlarmsPage(const CommonState& s, uint8_t subPage, const DisplayLabels& labels, DisplayFrame& f) {
    const uint32_t mask = s.alarms.activeMask;
    char buf[48];
    f.clear();
    if (mask == 0) {
        f.setTitle("ALARMS");
        f.addBody(0, "No active alarms");  // defensive: the scheduler never shows it
        return;
    }
    const uint8_t total = alarmCount(mask);
    const uint8_t pages = alarmSubPageCount(mask);
    const uint8_t page = static_cast<uint8_t>(subPage % pages);
    if (pages > 1) {
        snprintf(buf, sizeof(buf), "ALARMS %u %u/%u", static_cast<unsigned>(total),
                 static_cast<unsigned>(page + 1), static_cast<unsigned>(pages));
    } else {
        snprintf(buf, sizeof(buf), "ALARMS %u", static_cast<unsigned>(total));
    }
    f.setTitle(buf);

    const uint8_t first = static_cast<uint8_t>(page * DISPLAY_ALARMS_PER_SUBPAGE);
    uint8_t index = 0;  // position of the set bit in ascending order
    uint8_t row = 0;
    for (uint8_t bit = 0; bit < 32 && row < DISPLAY_ALARMS_PER_SUBPAGE; ++bit) {
        if ((mask & (1u << bit)) == 0) {
            continue;
        }
        if (index >= first) {
            alarmLabel(bit, labels, buf, sizeof(buf));
            f.addBody(row, buf);
            ++row;
        }
        ++index;
    }
}

// ---- Special screens ----

void renderSetupScreen(const NetworkStatus& n, DisplayFrame& f) {
    char buf[48];
    f.clear();
    f.setTitle("SETUP MODE");
    f.addBody(0, n.apSsid[0] != '\0' ? n.apSsid : "--");
    f.addBody(1, "(open, no password)");
    snprintf(buf, sizeof(buf), "Open: %s", n.apIp[0] != '\0' ? n.apIp : SETUP_DEFAULT_AP_IP);
    f.addBody(2, buf);
}

void renderResetScreen(ResetGatePhase phase, uint8_t secondsLeft, DisplayFrame& f) {
    char buf[16];
    f.clear();
    switch (phase) {
        case ResetGatePhase::Countdown: {
            f.setTitle("FACTORY RESET");
            f.addBody(0, "DI1 held");
            snprintf(buf, sizeof(buf), "%u s", static_cast<unsigned>(secondsLeft));
            f.addCentered(DisplayFont::Large, RESET_DIGITS_BASELINE, buf);
            const uint8_t left = secondsLeft < RESET_COUNTDOWN_S ? secondsLeft : RESET_COUNTDOWN_S;
            f.setBar(RESET_BAR_X, RESET_BAR_Y, RESET_BAR_W, RESET_BAR_H,
                     static_cast<uint8_t>((RESET_COUNTDOWN_S - left) * 10));
            break;
        }
        case ResetGatePhase::Confirmed:
            f.setTitle("FACTORY RESET");
            f.addBody(1, "Reset confirmed");
            f.addBody(2, "Settings erased");
            break;
        case ResetGatePhase::Aborted:
            f.setTitle("FACTORY RESET");
            f.addBody(1, "Reset aborted");
            f.addBody(2, "Settings kept");
            break;
        case ResetGatePhase::Inactive:
        default:
            break;  // cleared frame
    }
}

void renderOtaScreen(const OtaStatus& o, DisplayFrame& f) {
    f.clear();
    f.setTitle("UPDATING");
    f.addBody(0, "Firmware update...");
    f.addBody(1, "Do not power off");
    if (o.source == OtaSource::Web) {
        f.addBody(2, "via web");
    } else if (o.source == OtaSource::Espota) {
        f.addBody(2, "via espota");
    }
}

void renderBootSplash(const char* projectName, const char* fwVersion, DisplayFrame& f) {
    char buf[64];
    f.clear();
    f.setTitle(projectName != nullptr ? projectName : "");
    snprintf(buf, sizeof(buf), "fw %s", fwVersion != nullptr ? fwVersion : "");
    f.addBody(1, buf);
    f.addBody(3, "starting...");
}
