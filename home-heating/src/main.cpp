#include <Arduino.h>
#include <Wire.h>
#include <BoardConfig.h>
#include <RelayBoot.h>

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

static const char* const PROJECT_NAME = "home-heating";

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
}

void loop() {
    delay(1000);
}
