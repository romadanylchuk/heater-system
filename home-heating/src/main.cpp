#include <Arduino.h>
#include <Wire.h>
#include <BoardConfig.h>
#include <RelayBoot.h>
#include <CoreServices.h>
#include <HardwareServices.h>
#include <ConnectivityServices.h>
#include "HomeHeatingHardware.h"
#include "HomeHeatingNet.h"
#include "HomeHeatingSchema.h"

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

static const char* const PROJECT_NAME = "home-heating";

static CommonState state{};
static CoreServices core(state, HOME_HEATING_SCHEMA, FW_VERSION);
static HardwareServices hw(state, core, HOME_HEATING_HW);
// Stage 04: Wi-Fi/setup AP, mDNS, MQTT + HA discovery, web OTA/espota,
// rollback health. Control never depends on it (it only posts commands).
static ConnectivityServices net(state, core, hw, HOME_HEATING_NET, HOME_HEATING_HW);

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

    core.begin(Wire);
    hw.begin(Wire);
    net.begin();  // after core + hardware: relays are already driven by HwRuntime
}

void loop() {
    static bool first = true;
    static uint32_t lastSlow = 0;
    const uint32_t start = millis();
    if (first || start - lastSlow >= CORE_LOOP_PERIOD_MS) {
        first = false;
        lastSlow = start;
        core.tick();
        hw.tick();
        net.tick();
    }
    // net.fastTick() first: it drains OTA start/end signals and applies the
    // relay inhibit before hw.fastTick() writes the relay port.
    net.fastTick();
    hw.fastTick();
    const uint32_t spent = millis() - start;
    delay(spent < HW_FAST_TICK_MS ? HW_FAST_TICK_MS - spent : 1);
}
