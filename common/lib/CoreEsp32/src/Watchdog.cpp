#include "Watchdog.h"
#include <esp_system.h>
#include <esp_task_wdt.h>

namespace Watchdog {

bool begin() {
    esp_err_t err = esp_task_wdt_init(TIMEOUT_S, true);
    // Subscribe the calling (loop) task. Arduino-ESP32 may already have added it
    // (ESP_ERR_INVALID_STATE in that case); either way the loop task ends up
    // subscribed, so the result is not treated as this call's failure.
    esp_task_wdt_add(nullptr);
    return err == ESP_OK;
}

void feed() {
    esp_task_wdt_reset();
}

ResetCause lastResetCause() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return ResetCause::PowerOn;
        case ESP_RST_EXT:
            return ResetCause::External;
        case ESP_RST_SW:
            return ResetCause::Software;
        case ESP_RST_PANIC:
            return ResetCause::Panic;
        case ESP_RST_TASK_WDT:
            return ResetCause::TaskWatchdog;
        case ESP_RST_INT_WDT:
            return ResetCause::InterruptWatchdog;
        case ESP_RST_WDT:
            return ResetCause::OtherWatchdog;
        case ESP_RST_BROWNOUT:
            return ResetCause::Brownout;
        case ESP_RST_DEEPSLEEP:
            return ResetCause::DeepSleep;
        default:
            return ResetCause::Unknown;
    }
}

}  // namespace Watchdog
