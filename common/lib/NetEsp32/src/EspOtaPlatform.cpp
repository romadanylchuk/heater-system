#include "EspOtaPlatform.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <string.h>

// D15: overrides the weak `bool verifyRollbackLater()` in the Arduino core
// (cores/esp32/esp32-hal-misc.c, a C file -- hence extern "C"). initArduino()
// auto-validates the running image when this returns false; returning true
// leaves a freshly-updated image in ESP_OTA_IMG_PENDING_VERIFY so the app
// confirms it itself (markRunningValid) after the 60 s health check, or rolls
// it back (rollbackAndReboot) on the 300 s deadline. A crash/reset before
// confirmation is rolled back by the (rollback-enabled) bootloader.
extern "C" bool verifyRollbackLater() { return true; }

namespace {

bool copyLabel(const esp_partition_t* part, char* out, size_t cap) {
    if (part == nullptr || out == nullptr || cap == 0) {
        return false;
    }
    const size_t len = strnlen(part->label, sizeof(part->label));
    if (len + 1 > cap) {
        return false;  // out untouched on overflow (OtaPlatform contract)
    }
    memcpy(out, part->label, len);
    out[len] = '\0';
    return true;
}

}  // namespace

OtaImageState EspOtaPlatform::runningImageState() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return OtaImageState::Unknown;
    }
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) != ESP_OK) {
        return OtaImageState::Unknown;  // e.g. factory partition (ESP_ERR_NOT_SUPPORTED)
    }
    switch (st) {
        case ESP_OTA_IMG_PENDING_VERIFY:
            return OtaImageState::PendingVerify;
        case ESP_OTA_IMG_VALID:
        case ESP_OTA_IMG_UNDEFINED:
            return OtaImageState::Valid;
        default:
            return OtaImageState::Unknown;
    }
}

bool EspOtaPlatform::runningLabel(char* out, size_t cap) {
    return copyLabel(esp_ota_get_running_partition(), out, cap);
}

bool EspOtaPlatform::bootLabel(char* out, size_t cap) { return copyLabel(esp_ota_get_boot_partition(), out, cap); }

bool EspOtaPlatform::markRunningValid() { return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK; }

void EspOtaPlatform::rollbackAndReboot() {
    // Only returns on failure (no other valid slot, ota_data write error).
    // No restart fallback here: ConnectivityRuntime logs the failure and calls
    // restart() itself (fix-7-0), keeping outputs inhibited meanwhile.
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

void EspOtaPlatform::restart() { esp_restart(); }

bool EspOtaPlatform::updateRunning() { return Update.isRunning(); }

void EspOtaPlatform::abortUpdate() { Update.abort(); }
