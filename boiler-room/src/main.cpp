#include <Arduino.h>
#include <Wire.h>
#include <BoardConfig.h>
#include <RelayBoot.h>
#include <CoreServices.h>
#include <HardwareServices.h>
#include <ConnectivityServices.h>
#include <WebServices.h>
#include <DisplayServices.h>
#include "BoilerRoomHardware.h"
#include "BoilerRoomNet.h"
#include "BoilerRoomSchema.h"
#include "BoilerRoomAlarms.h"
#include "BoilerRoomHa.h"
#include "BoilerRoomJson.h"
#include "BoilerRoomPages.h"
#include "BoilerRoomRuntime.h"
#include "BoilerRoomStatus.h"

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

static const char* const PROJECT_NAME = "boiler-room";

static CommonState state{};
// Stage 07: the controller AppState slice, written only by ctl.tick() on the
// loop task; the web "ctl" member, the OLED pages and HA entities read it.
static BoilerRoomStatus ctlStatus{};
static CoreServices core(state, BOILER_ROOM_SCHEMA, FW_VERSION);
static HardwareServices hw(state, core, BOILER_ROOM_HW);
// Stage 07: the boiler-room controller. Pure logic over CommonState + settings;
// drives the pumps only through the RelayBank request slots. Never depends on
// Wi-Fi/MQTT/HA (a lost link only gates the "no need" flag, D7).
static BoilerRoomRuntime ctl(state, ctlStatus, core.config(), core.events(), hw.runtime().relays());
// Stage 04: Wi-Fi/setup AP, mDNS, MQTT + HA discovery, web OTA/espota,
// rollback health. Control never depends on it (it only posts commands).
static ConnectivityServices net(state, core, hw, BOILER_ROOM_NET, BOILER_ROOM_HW, BOILER_ROOM_HA_ENTITIES,
    BOILER_ROOM_HA_ENTITY_COUNT);
// Stage 05: session-gated SPA/API, JSON snapshots, captive DNS. Handlers never
// touch CommonState; writes only through the command queue.
static WebServices web(state, core, net, BOILER_ROOM_SCHEMA, BOILER_ROOM_HW);
// Stage 06: SSD1306 OLED. All display I2C runs on the loop task, last in each
// fast pass (<= 2 tile rows), at the unchanged 100 kHz bus clock. Read-only view.
static DisplayServices display(state, core, BOILER_ROOM_HW, PROJECT_NAME, BOILER_ROOM_ALARMS,
    BOILER_ROOM_ALARM_COUNT);

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
    if (ctl.begin(core.time().monoMs()) != BoilerRoomRuntimeStatus::Ok) {
        Serial.println("[ctl] settings missing: P1 forced ON");
    }
    bindBoilerRoomHaStatus(&ctlStatus);
    web.setStateExtension(boilerRoomStateJson, &ctlStatus);
    // Controller pages: after beginEarly(), strictly before display.begin().
    display.addPage({"Boiler", renderBoilerPage, &ctlStatus});
    display.addPage({"Accu", renderAccuPage, &ctlStatus});
    display.addPage({"Supply", renderSupplyPage, &ctlStatus});
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
        // relay state) -> ctl (decides on this tick's data, writes the relay
        // request slots before hw.fastTick() applies them) -> net (publishes
        // the new status) -> web (snapshots include the new "ctl").
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
