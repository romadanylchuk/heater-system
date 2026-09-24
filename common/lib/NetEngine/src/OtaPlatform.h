#pragma once
#include <stddef.h>
#include <stdint.h>

// Pure OTA hardware port interface (stage 04). NetEsp32's EspOtaPlatform
// implements it over esp_ota_ops.h for firmware; common/test/fakes/
// FakeOtaPlatform implements it for native tests. OtaGuard/OtaBoot/
// OtaHealthMonitor depend only on this interface and on OtaSource/
// OtaBootOutcome (CommonState.h) -- no esp_ota_ops.h/Arduino includes here.
enum class OtaImageState : uint8_t { Unknown, Valid, PendingVerify };
constexpr size_t OTA_LABEL_MAX = 16;

class OtaPlatform {
public:
    virtual ~OtaPlatform() = default;

    // Validation state of the currently-running app partition.
    virtual OtaImageState runningImageState() = 0;

    // Label of the currently-running partition (e.g. "app0"/"app1"/"factory").
    // False on overflow/failure; out left untouched on false.
    virtual bool runningLabel(char* out, size_t cap) = 0;

    // Label of the partition selected to boot next. Differs from
    // runningLabel() only right after a successful OTA, before the reboot.
    virtual bool bootLabel(char* out, size_t cap) = 0;

    // esp_ota_mark_app_valid_cancel_rollback(): cancels the rollback timer,
    // confirming the running image is healthy.
    virtual bool markRunningValid() = 0;

    // esp_ota_mark_app_invalid_rollback_and_reboot(): marks the running image
    // invalid and reboots into the previous partition. Normally does not
    // return; it CAN return on failure (no other valid slot, ota_data/NVS
    // write error). Callers must treat a return as "still running the failed
    // image" and fall back to restart().
    virtual void rollbackAndReboot() = 0;

    // Plain reboot (esp_restart()). Fallback when rollbackAndReboot() returns:
    // with app rollback enabled, rebooting while the running image is still
    // pending-verify makes the bootloader roll back / not re-confirm it.
    // Does not return on real hardware (fakes return).
    virtual void restart() = 0;

    // The global Arduino `Update` object shared by ElegantOTA and espota
    // (final-check S3). updateRunning() == Update.isRunning(): a flash
    // session is open. abortUpdate() == Update.abort(): closes a session
    // abandoned by a web client, so later OTAs are not refused with "already
    // running". Loop task only, and only when no writer can be active (the
    // web session already ended or stalled).
    virtual bool updateRunning() = 0;
    virtual void abortUpdate() = 0;
};
