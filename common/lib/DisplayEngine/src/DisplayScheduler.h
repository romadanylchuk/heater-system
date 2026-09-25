#pragma once
#include <stdint.h>

// Display screen state machine (stage 06, D11/D12/D13): page rotation with a
// dwell latched at page entry, the dynamic Alarms page, the alarm jump + 30 s
// hold, the setup-mode interleave, the OTA screen, the boot reset-result
// screen and the free-running burn-in shift step. Pure: time is injected and
// every elapsed check is unsigned subtraction, so millis() wrap is harmless.
constexpr uint32_t DISPLAY_ALARM_HOLD_MS    = 30000;   // fixed, not a setting
constexpr uint32_t DISPLAY_ALARM_SUBPAGE_MS = 2500;
constexpr uint32_t DISPLAY_RESET_RESULT_MS  = 3000;
constexpr uint8_t  DISPLAY_SETUP_ALARM_SLOT = 3;       // in setup mode every 3rd slot shows Alarms (if any)
constexpr uint32_t DISPLAY_ROTATE_MS_MIN    = 2000;    // internal re-clamp of rotatePeriodMs
constexpr uint32_t DISPLAY_ROTATE_MS_MAX    = 60000;

enum class DisplayScreen : uint8_t { Page, Alarms, Setup, Ota, ResetResult };

struct DisplayInputs {
    uint32_t nowMs;            // monotonic, wraps; all math is unsigned subtraction
    uint32_t alarmMask;        // CommonState.alarms.activeMask
    bool setupActive;          // network.setupApActive
    bool otaActive;            // ota.inProgress
    uint32_t rotatePeriodMs;   // displayRotatePeriodMs(setting); re-clamped to [2000,60000] internally
    uint8_t alarmSubPages;     // alarmSubPageCount(alarmMask)
};

struct DisplayView {
    DisplayScreen screen;
    uint8_t pageIndex;         // valid for Page (0..pageCount-1); 0 otherwise
    uint8_t subPage;           // valid for Alarms; 0 otherwise
    uint32_t shiftStep;        // feed to pixelShiftOffset()
    bool changed;              // screen/pageIndex/subPage/shiftStep differ from the previous update()
};

class DisplayScheduler {
public:
    // pageCount = rotation pages excluding Alarms (>= 1; 0 is treated as 1).
    void begin(uint32_t nowMs, uint8_t pageCount, bool showResetResult);
    DisplayView update(const DisplayInputs& in);
    bool alarmHoldActive() const;

private:
    enum class Override : uint8_t { None, ResetResult, Ota, Setup };

    void restartRotation(uint32_t nowMs, uint32_t periodMs);
    DisplayView makeView(DisplayScreen screen, uint8_t pageIndex, uint8_t subPage);

    uint8_t _pageCount = 1;
    uint8_t _pageIndex = 0;            // == _pageCount means Alarms-in-rotation
    bool _onAlarmsInRotation = false;
    uint32_t _pageStartMs = 0;
    uint32_t _dwellMs = DISPLAY_ROTATE_MS_MIN;

    bool _holdActive = false;
    uint32_t _holdStartMs = 0;

    uint32_t _prevMask = 0;
    bool _firstUpdate = true;

    uint32_t _shiftStep = 0;
    uint32_t _shiftStartMs = 0;
    uint32_t _shiftPeriodMs = DISPLAY_ROTATE_MS_MIN;

    bool _resetResultActive = false;
    uint32_t _beginMs = 0;

    uint32_t _slotIndex = 0;
    uint32_t _slotStartMs = 0;

    Override _override = Override::None;   // last higher-priority screen (detects its end)

    DisplayView _last = {DisplayScreen::Page, 0, 0, 0, false};
};
