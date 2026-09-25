#include "DisplayServices.h"
#include <Arduino.h>
#include <BoardConfig.h>
#include <DisplaySettings.h>
#include <EventTypes.h>
#include <HardwareStatus.h>

namespace {

const uint8_t* u8g2FontFor(DisplayFont f) {
    switch (f) {
        case DisplayFont::Small: return u8g2_font_5x8_tr;
        case DisplayFont::Large: return u8g2_font_10x20_tr;
        case DisplayFont::Normal:
        default: return u8g2_font_6x10_tr;
    }
}

constexpr uint8_t DISPLAY_TILE_COLS = DISPLAY_WIDTH / 8;  // 16 tiles per row

}  // namespace

DisplayServices::DisplayServices(CommonState& state, CoreServices& core, const HwProjectConfig& hw,
    const char* projectName, const AlarmDescriptor* alarms, size_t alarmCount)
    : _state(state),
      _core(core),
      _labels{alarms, alarmCount, hw.sensors, hw.sensorCount},
      _projectName(projectName) {}

bool DisplayServices::addPage(const DisplayPageDesc& page) {
    if (_began) {
        return false;
    }
    return _pages.add(page);
}

void DisplayServices::beginEarly(TwoWire& wire) {
    _wire = &wire;
    if (!Ssd1306Panel::probe(wire)) {
        _available = false;
        _lastProbeMs = millis();
        Serial.printf("[display] SSD1306 @0x%02X not found, continuing without display\n", SSD1306_ADDR);
    } else if (!initPanel(displayContrastFromPercent(DISPLAY_BRIGHT_DEFAULT))) {
        markUnavailable();
    } else {
        _available = true;
        _failStreak = 0;
        renderBootSplash(_projectName, _core.fwVersion(), _frame);
        drawToPanel(_frame, PixelOffset{0, 0});
        _tiles.scan(_panel.getBufferPtr());
        // A transient NACK leaves the row dirty; flushRows() gives up (and marks
        // the panel unavailable) after DISPLAY_FAIL_LIMIT consecutive failures.
        while (_available && _tiles.hasPending()) {
            flushRows(DISPLAY_TILE_ROWS);
        }
        if (_available) {
            _panel.setPowerSave(0);  // un-blank only after the splash is in panel RAM (D19)
            if (_panel.takeError()) {
                markUnavailable();
            }
        }
    }
    // Always registered: the hook is a no-op while the panel is unavailable.
    _core.setResetCountdownHook(&DisplayServices::resetHook, this);
}

void DisplayServices::resetHook(ResetGatePhase phase, uint8_t secondsLeft, void* ctx) {
    DisplayServices* self = static_cast<DisplayServices*>(ctx);
    if (self == nullptr || !self->_available || phase == ResetGatePhase::Inactive) {
        return;  // boot splash stays; never fatal, never logs from the countdown loop
    }
    renderResetScreen(phase, secondsLeft, self->_frame);
    self->drawToPanel(self->_frame, PixelOffset{0, 0});
    self->_tiles.scan(self->_panel.getBufferPtr());
    // Synchronous flush of the changed rows only (digits/bar, ~2-4 rows, once
    // per second). Relays are already OFF and runBootCountdown feeds the WDT.
    self->flushRows(DISPLAY_TILE_ROWS);
    if (phase == ResetGatePhase::Aborted || phase == ResetGatePhase::Confirmed) {
        self->_resetResult = phase;
    }
}

void DisplayServices::begin() {
    _idxRotate = _core.config().indexOf(DISPLAY_KEY_ROTATE_S);
    _idxBright = _core.config().indexOf(DISPLAY_KEY_BRIGHTNESS);
    if (!appendSharedPages(_pages, &_labels)) {
        Serial.println("[display] page registry full, shared pages not added");
    }
    const uint32_t now = millis();
    _sched.begin(now, static_cast<uint8_t>(_pages.count()), _resetResult != ResetGatePhase::Inactive);
    _began = true;
    _forceRender = true;
    if (!_available) {
        logMissingOnce();  // deferred from beginEarly: the event log was not ready then
        _lastProbeMs = now;
    }
    Serial.printf("[display] pages=%u available=%u\n", static_cast<unsigned>(_pages.count()),
        static_cast<unsigned>(_available));
}

void DisplayServices::fastTick(uint32_t passElapsedMs) {
#ifdef DEBUG_BUILD
    const uint32_t t0 = micros();
    tickInner(passElapsedMs);
    const uint32_t dt = micros() - t0;
    if (dt > _maxTickUs) {
        _maxTickUs = dt;
    }
    const uint32_t now = millis();
    if (now - _lastTimingLogMs >= 60000u) {
        _lastTimingLogMs = now;
        Serial.printf("[display] max tick %u us\n", static_cast<unsigned>(_maxTickUs));
        _maxTickUs = 0;
    }
#else
    tickInner(passElapsedMs);
#endif
}

void DisplayServices::tickInner(uint32_t passElapsedMs) {
    if (!_began) {
        return;
    }
    const uint32_t now = millis();
    if (!_available) {
        if (now - _lastProbeMs >= DISPLAY_RETRY_MS) {
            retryProbe(now);
        }
        return;
    }

    const int32_t rotateS = settingOr(_idxRotate, DISPLAY_ROTATE_S_DEFAULT);
    const int32_t bright = settingOr(_idxBright, DISPLAY_BRIGHT_DEFAULT);

    const uint8_t contrast = displayContrastFromPercent(bright);
    if (contrast != _appliedContrast) {
        _panel.setContrast(contrast);  // one short command transaction
        if (_panel.takeError()) {
            if (++_failStreak >= DISPLAY_FAIL_LIMIT) {
                markUnavailable();
                return;
            }
        } else {
            _appliedContrast = contrast;
        }
    }

    DisplayInputs in{};
    in.nowMs = now;
    in.alarmMask = _state.alarms.activeMask;
    in.setupActive = _state.network.setupApActive;
    in.otaActive = _state.ota.inProgress;
    in.rotatePeriodMs = displayRotatePeriodMs(rotateS);
    in.alarmSubPages = alarmSubPageCount(in.alarmMask);
    const DisplayView view = _sched.update(in);

    if (view.changed || _forceRender || (now - _lastRenderMs >= DISPLAY_RENDER_PERIOD_MS && !_tiles.hasPending())) {
        renderView(view);
        _lastRenderMs = now;
        _forceRender = false;
    }

    // Count the display work done above (contrast, render, probe) as well.
    flushRows(displayRowsForPass(passElapsedMs + (millis() - now)));
}

void DisplayServices::retryProbe(uint32_t now) {
    _lastProbeMs = now;
    if (_wire == nullptr || !Ssd1306Panel::probe(*_wire)) {
        return;
    }
    const uint8_t contrast = displayContrastFromPercent(settingOr(_idxBright, DISPLAY_BRIGHT_DEFAULT));
    Ssd1306Panel::setSkipInitDelays(true);  // no reset line: avoid a ~300 ms loop stall
    const bool ok = initPanel(contrast);
    Ssd1306Panel::setSkipInitDelays(false);
    if (!ok) {
        return;
    }
    _available = true;
    _failStreak = 0;
    _powerOnPending = true;  // un-blank once every row has been re-sent
    _forceRender = true;
    Serial.println("[display] recovered");
}

void DisplayServices::renderView(const DisplayView& view) {
    switch (view.screen) {
        case DisplayScreen::Page: {
            const DisplayPageDesc* page = _pages.at(view.pageIndex);
            if (page != nullptr) {
                page->render(_state, _frame, page->ctx);
            } else {
                _frame.clear();
            }
            break;
        }
        case DisplayScreen::Alarms: renderAlarmsPage(_state, view.subPage, _labels, _frame); break;
        case DisplayScreen::Setup: renderSetupScreen(_state.network, _frame); break;
        case DisplayScreen::Ota: renderOtaScreen(_state.ota, _frame); break;
        case DisplayScreen::ResetResult: renderResetScreen(_resetResult, 0, _frame); break;
    }
    drawToPanel(_frame, pixelShiftOffset(view.shiftStep));
    _tiles.scan(_panel.getBufferPtr());
}

void DisplayServices::drawToPanel(const DisplayFrame& frame, PixelOffset off) {
    _panel.clearBuffer();
    _panel.setFontPosBaseline();
    for (uint8_t i = 0; i < frame.lineCount && i < DISPLAY_MAX_LINES; ++i) {
        const DisplayTextLine& line = frame.line[i];
        _panel.setFont(u8g2FontFor(line.font));
        _panel.drawStr(line.x + off.dx, line.baseline + off.dy, line.text);
    }
    if (frame.titleRule) {
        _panel.drawHLine(off.dx, DISPLAY_TITLE_RULE_Y + off.dy, DISPLAY_CONTENT_WIDTH);
    }
    if (frame.bar.used && frame.bar.w >= 2 && frame.bar.h >= 2) {
        const DisplayBar& b = frame.bar;
        _panel.drawFrame(b.x + off.dx, b.y + off.dy, b.w, b.h);
        const uint8_t pct = b.percent > 100 ? 100 : b.percent;
        const uint16_t fill = static_cast<uint16_t>((b.w - 2) * pct / 100);
        if (fill > 0 && b.h > 2) {
            _panel.drawBox(b.x + off.dx + 1, b.y + off.dy + 1, fill, b.h - 2);
        }
    }
}

void DisplayServices::flushRows(uint8_t maxRows) {
    for (uint8_t sent = 0; sent < maxRows && _available; ++sent) {
        const int8_t row = _tiles.nextRow();
        if (row < 0) {
            break;
        }
        _panel.updateDisplayArea(0, static_cast<uint8_t>(row), DISPLAY_TILE_COLS, 1);
        if (_panel.takeError()) {
            // The row stays dirty; a single EMI glitch is not a missing panel.
            if (++_failStreak >= DISPLAY_FAIL_LIMIT) {
                markUnavailable();
            }
            return;
        }
        _tiles.markSent(static_cast<uint8_t>(row), _panel.getBufferPtr());
        _failStreak = 0;
    }
    if (_available && _powerOnPending && !_tiles.hasPending()) {
        _powerOnPending = false;
        _panel.setPowerSave(0);
        if (_panel.takeError() && ++_failStreak >= DISPLAY_FAIL_LIMIT) {
            markUnavailable();
        }
    }
}

bool DisplayServices::initPanel(uint8_t contrast) {
    _panel.takeError();  // drop any stale latch
    _panel.initDisplay();  // init sequence; leaves the panel blanked (power save on)
    _panel.setContrast(contrast);
    _appliedContrast = contrast;
    _tiles.invalidateAll();
    return !_panel.takeError();
}

void DisplayServices::markUnavailable() {
    _available = false;
    _powerOnPending = false;
    _failStreak = 0;
    _lastProbeMs = millis();
    Serial.printf("[display] SSD1306 @0x%02X not responding, retry every %u s\n", SSD1306_ADDR,
        static_cast<unsigned>(DISPLAY_RETRY_MS / 1000));
    if (_began) {
        logMissingOnce();
    }
}

void DisplayServices::logMissingOnce() {
    if (_everLoggedMissing) {
        return;
    }
    _everLoggedMissing = true;
    _core.events().logEvent(toU16(EventType::DiagnosticWarning), EVENT_SOURCE_DIAG_BASE + DIAG_CODE_DISPLAY_MISSING,
        0.0f, 0.0f, EventReason::Logic);
}

int32_t DisplayServices::settingOr(int idx, int32_t fallback) const {
    return idx >= 0 ? _core.config().getInt(static_cast<size_t>(idx)) : fallback;
}
