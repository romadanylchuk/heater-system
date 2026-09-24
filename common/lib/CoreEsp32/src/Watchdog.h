#pragma once
#include <stdint.h>
#include "EventTypes.h"

// Task watchdog (IDF task WDT) + reset-cause capture (D14). esp_task_wdt_init()
// reconfigures the WDT that Arduino-ESP32 already set up (IDF 4.4 allows
// re-init/update); esp_task_wdt_add(nullptr) subscribes the calling (loop) task.
namespace Watchdog {

constexpr uint32_t TIMEOUT_S = 15;

// esp_task_wdt_init(TIMEOUT_S, true) + esp_task_wdt_add(nullptr). Returns false
// if esp_task_wdt_init failed; the caller sets state.diag.watchdogError in that
// case and boot continues (D14).
bool begin();

// esp_task_wdt_reset() -- call every loop tick and during the DI1 boot countdown.
void feed();

// Maps esp_reset_reason() to the pure ResetCause enum (EventTypes.h).
ResetCause lastResetCause();

}  // namespace Watchdog
