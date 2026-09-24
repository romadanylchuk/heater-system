#pragma once
#include <OtaPlatform.h>

// OtaPlatform over ESP-IDF esp_ota_ops (D15). Stateless; all calls are made
// from the loop task by ConnectivityRuntime.
//
// EspOtaPlatform.cpp also defines the strong `extern "C" bool
// verifyRollbackLater()` that overrides the Arduino core's weak default, so a
// freshly-updated image boots as ESP_OTA_IMG_PENDING_VERIFY and is confirmed
// by the app itself after OtaHealthMonitor's health check. Because the
// override lives in this translation unit, the linker only pulls it in when
// the firmware references EspOtaPlatform (the ConnectivityServices facade).
class EspOtaPlatform : public OtaPlatform {
public:
    OtaImageState runningImageState() override;
    bool runningLabel(char* out, size_t cap) override;
    bool bootLabel(char* out, size_t cap) override;
    bool markRunningValid() override;
    void rollbackAndReboot() override;
    void restart() override;
    bool updateRunning() override;
    void abortUpdate() override;
};
