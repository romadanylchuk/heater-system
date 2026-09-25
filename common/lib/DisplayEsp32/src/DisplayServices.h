#pragma once
#include <Wire.h>
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <CoreServices.h>
#include <Descriptors.h>
#include <DisplayFrame.h>
#include <DisplayPages.h>
#include <DisplayScheduler.h>
#include <HwConfig.h>
#include <PixelShift.h>
#include <TileScheduler.h>
#include "Ssd1306Panel.h"

// Wiring facade for the SSD1306 OLED (stage 06), constructed by both
// main.cpp files. All display I2C runs on the loop task (D1): beginEarly()
// in setup() right after the SAFETY relay-off statements, the DI1 reset
// countdown hook inside core.begin(), and fastTick() as the LAST call of
// every 100 ms fast pass (after hw.fastTick(), so relay/K1 writes are never
// delayed). fastTick() sends at most displayRowsForPass() changed 128-byte
// tile rows per pass (2 / 1 / 0). A missing or failing panel never blocks
// boot or control: available() is false, fastTick() only re-probes every
// DISPLAY_RETRY_MS, and one DiagnosticWarning event is logged per boot.
// The display never writes CommonState and never changes the bus clock.
constexpr uint32_t DISPLAY_RENDER_PERIOD_MS = 1000;  // periodic redraw (values look live)
constexpr uint32_t DISPLAY_RETRY_MS = 30000;         // re-probe interval while unavailable
constexpr uint8_t DISPLAY_FAIL_LIMIT = 3;            // consecutive failed row sends -> unavailable

class DisplayServices {
public:
    // alarms: controller alarm table (stages 07/08) INDEXED BY ALARM BIT --
    // entry i describes CommonState.alarms.activeMask bit i (i < 24); bits
    // 24..31 are the "Sensor <name> missing" alarms and come from hw.sensors.
    // The constructor touches no hardware.
    DisplayServices(CommonState& state, CoreServices& core, const HwProjectConfig& hw, const char* projectName,
        const AlarmDescriptor* alarms = nullptr, size_t alarmCount = 0);

    // Controller pages (07/08), in rotation order. Only before begin(); false
    // afterwards or when the registry is full. Network/Sensors follow them.
    bool addPage(const DisplayPageDesc& page);

    // After Wire.begin + RelayBoot::forceAllOff, before core.begin(): probe,
    // init + boot splash when present, and ALWAYS registers the reset-countdown
    // hook (a no-op while the panel is absent).
    void beginEarly(TwoWire& wire);

    // After net.begin(): setting indices, shared pages, scheduler, deferred
    // one-time missing-display event.
    void begin();

    // Last call of every fast pass; passElapsedMs = ms already spent in the pass.
    void fastTick(uint32_t passElapsedMs);

    bool available() const { return _available; }

private:
    static void resetHook(ResetGatePhase phase, uint8_t secondsLeft, void* ctx);

    void tickInner(uint32_t passElapsedMs);
    void retryProbe(uint32_t now);
    void renderView(const DisplayView& view);
    void drawToPanel(const DisplayFrame& frame, PixelOffset off);
    void flushRows(uint8_t maxRows);
    bool initPanel(uint8_t contrast);
    void markUnavailable();
    void logMissingOnce();
    int32_t settingOr(int idx, int32_t fallback) const;

    CommonState& _state;
    CoreServices& _core;
    DisplayLabels _labels;
    const char* _projectName;

    TwoWire* _wire = nullptr;
    Ssd1306Panel _panel;
    TileScheduler _tiles;
    DisplayScheduler _sched;
    DisplayPageList _pages;
    DisplayFrame _frame = {};

    bool _available = false;
    bool _everLoggedMissing = false;
    bool _powerOnPending = false;  // panel re-initialised: un-blank once every row is sent
    uint8_t _failStreak = 0;
    uint32_t _lastProbeMs = 0;
    uint32_t _lastRenderMs = 0;
    bool _forceRender = false;

    int _idxRotate = -1;
    int _idxBright = -1;
    uint8_t _appliedContrast = 0;

    bool _began = false;
    ResetGatePhase _resetResult = ResetGatePhase::Inactive;

#ifdef DEBUG_BUILD
    uint32_t _maxTickUs = 0;
    uint32_t _lastTimingLogMs = 0;
#endif
};
