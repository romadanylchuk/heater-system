#include <Arduino.h>
#include <Wire.h>
#include <BoardConfig.h>
#include <RelayBoot.h>
#include <CoreServices.h>
#include <HardwareServices.h>
#include <ConnectivityServices.h>
#include <WebServices.h>
#include <DisplayServices.h>
#include "HomeHeatingHardware.h"
#include "HomeHeatingNet.h"
#include "HomeHeatingSchema.h"
#include "HomeHeatingAlarms.h"
#include "HomeHeatingHa.h"
#include "HomeHeatingJson.h"
#include "HomeHeatingPages.h"
#include "HomeHeatingRuntime.h"
#include "HomeHeatingStatus.h"

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

static const char* const PROJECT_NAME = "home-heating";

static CommonState state{};
// Stage 08: the controller AppState slice, written only by ctl.tick() on the
// loop task; the web "ctl" member, the OLED pages and HA entities read it.
static HomeHeatingStatus ctlStatus{};
static CoreServices core(state, HOME_HEATING_SCHEMA, FW_VERSION);
static HardwareServices hw(state, core, HOME_HEATING_HW);
// Stage 08: the home-heating controller (P4, K2, K1 mixing valve, "no need").
// Pure logic over CommonState + settings; drives P4/K2 only through the
// RelayBank control slot (60 s lock) and K1 only through K1Driver (Control
// owner). Never depends on Wi-Fi/MQTT/HA.
static HomeHeatingRuntime ctl(state, ctlStatus, core.config(), core.events(), hw.runtime().relays(),
    hw.runtime().k1(), hw.runtime().antiSeize());
// Stage 04: Wi-Fi/setup AP, mDNS, MQTT + HA discovery, web OTA/espota,
// rollback health. Control never depends on it (it only posts commands).
static ConnectivityServices net(state, core, hw, HOME_HEATING_NET, HOME_HEATING_HW, HOME_HEATING_HA_ENTITIES,
    HOME_HEATING_HA_ENTITY_COUNT);
// Stage 05: session-gated SPA/API, JSON snapshots, captive DNS. Handlers never
// touch CommonState; writes only through the command queue.
static WebServices web(state, core, net, HOME_HEATING_SCHEMA, HOME_HEATING_HW);
// Stage 06: SSD1306 OLED. All display I2C runs on the loop task, last in each
// fast pass (<= 2 tile rows), at the unchanged 100 kHz bus clock. Read-only view.
static DisplayServices display(state, core, HOME_HEATING_HW, PROJECT_NAME, HOME_HEATING_ALARMS,
    HOME_HEATING_ALARM_COUNT);

void setup() {
    // SAFETY: must remain the first statements of setup()
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    const bool relaysOff = RelayBoot::forceAllOff(Wire);

    Serial.begin(115200);
    Serial.printf("\n=== %s  fw %s ===\n", PROJECT_NAME, FW_VERSION);
    if (!relaysOff) {
        Serial.printf("[boot] relay PCF8574 @0x%02X all-OFF write failed (no ACK), continuing\n",
                       PCF8574_RELAY_ADDR);
    }

    display.beginEarly(Wire);  // after SAFETY relay-off: shows DI1 reset countdown
    core.begin(Wire);
    hw.begin(Wire);
    if (ctl.begin(core.time().monoMs()) != HomeHeatingRuntimeStatus::Ok) {
        Serial.println("[ctl] settings missing: outputs held off");
    }
    bindHomeHeatingHaStatus(&ctlStatus);
    web.setStateExtension(homeHeatingStateJson, &ctlStatus);
    // Controller pages: after beginEarly(), strictly before display.begin().
    display.addPage({"Heating", renderHeatingPage, &ctlStatus});
    display.addPage({"DHW", renderDhwPage, &ctlStatus});
    web.attach();  // before net.begin(): injects the gate + route installer
    net.begin();  // after core + hardware: relays are already driven by HwRuntime
    display.begin();
}

void loop() {
    static bool first = true;
    static uint32_t lastSlow = 0;
    const uint32_t start = millis();
    if (first || start - lastSlow >= CORE_LOOP_PERIOD_MS) {
        first = false;
        lastSlow = start;
        // D25 order: core (settings/commands/time) -> hw (fresh sensors, DI,
        // relay state, K1 motion) -> ctl (decides on this tick's data, writes
        // the relay control slots / K1 requests before hw.fastTick() applies
        // them) -> net (publishes the new status) -> web (snapshots include "ctl").
        core.tick();
        hw.tick();
        ctl.tick(core.time().monoMs());
        net.tick();
        web.tick();
    }
    // net.fastTick() first: it drains OTA start/end signals and applies the
    // relay inhibit before hw.fastTick() writes the relay port.
    net.fastTick();
    web.fastTick();
    hw.fastTick();
    display.fastTick(millis() - start);  // last: never delays relay/K1 writes
    const uint32_t spent = millis() - start;
    delay(spent < HW_FAST_TICK_MS ? HW_FAST_TICK_MS - spent : 1);
}
