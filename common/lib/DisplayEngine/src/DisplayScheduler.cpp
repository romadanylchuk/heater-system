#include "DisplayScheduler.h"

namespace {

uint32_t clampPeriod(uint32_t periodMs) {
    if (periodMs < DISPLAY_ROTATE_MS_MIN) {
        return DISPLAY_ROTATE_MS_MIN;
    }
    if (periodMs > DISPLAY_ROTATE_MS_MAX) {
        return DISPLAY_ROTATE_MS_MAX;
    }
    return periodMs;
}

bool elapsed(uint32_t nowMs, uint32_t startMs, uint32_t periodMs) {
    return static_cast<uint32_t>(nowMs - startMs) >= periodMs;
}

uint8_t subPageAt(uint32_t nowMs, uint32_t startMs, uint8_t subPages) {
    const uint32_t count = subPages == 0 ? 1u : subPages;
    const uint32_t slot = static_cast<uint32_t>(nowMs - startMs) / DISPLAY_ALARM_SUBPAGE_MS;
    return static_cast<uint8_t>(slot % count);
}

uint32_t alarmsDwell(uint32_t periodMs, uint8_t subPages) {
    const uint32_t subDwell = static_cast<uint32_t>(subPages) * DISPLAY_ALARM_SUBPAGE_MS;
    return subDwell > periodMs ? subDwell : periodMs;
}

}  // namespace

void DisplayScheduler::begin(uint32_t nowMs, uint8_t pageCount, bool showResetResult) {
    _pageCount = pageCount == 0 ? 1 : pageCount;
    _pageIndex = 0;
    _onAlarmsInRotation = false;
    _pageStartMs = nowMs;
    _dwellMs = DISPLAY_ROTATE_MS_MIN;
    _holdActive = false;
    _holdStartMs = nowMs;
    _prevMask = 0;
    _firstUpdate = true;
    _shiftStep = 0;
    _shiftStartMs = nowMs;
    _shiftPeriodMs = DISPLAY_ROTATE_MS_MIN;
    _resetResultActive = showResetResult;
    _beginMs = nowMs;
    _slotIndex = 0;
    _slotStartMs = nowMs;
    _override = Override::None;
    _last = {DisplayScreen::Page, 0, 0, 0, false};
}

bool DisplayScheduler::alarmHoldActive() const {
    return _holdActive;
}

void DisplayScheduler::restartRotation(uint32_t nowMs, uint32_t periodMs) {
    _pageIndex = 0;
    _onAlarmsInRotation = false;
    _pageStartMs = nowMs;
    _dwellMs = periodMs;
}

DisplayView DisplayScheduler::makeView(DisplayScreen screen, uint8_t pageIndex, uint8_t subPage) {
    DisplayView view;
    view.screen = screen;
    view.pageIndex = screen == DisplayScreen::Page ? pageIndex : 0;
    view.subPage = screen == DisplayScreen::Alarms ? subPage : 0;
    view.shiftStep = _shiftStep;
    view.changed = _firstUpdate || view.screen != _last.screen || view.pageIndex != _last.pageIndex ||
                   view.subPage != _last.subPage || view.shiftStep != _last.shiftStep;
    _last = view;
    _firstUpdate = false;
    return view;
}

DisplayView DisplayScheduler::update(const DisplayInputs& in) {
    const uint32_t now = in.nowMs;
    const uint32_t period = clampPeriod(in.rotatePeriodMs);
    const uint32_t mask = in.alarmMask;

    // 2. Rising edges (first update: _prevMask == 0, so boot alarms jump too).
    const uint32_t rising = mask & ~_prevMask;
    _prevMask = mask;

    // 3. Free-running pixel shift, period latched per step; first update also
    //    latches the dwell of the initial page.
    if (_firstUpdate) {
        _shiftStartMs = now;
        _shiftPeriodMs = period;
        _pageStartMs = now;
        _dwellMs = period;
    } else if (elapsed(now, _shiftStartMs, _shiftPeriodMs)) {
        ++_shiftStep;
        _shiftStartMs = now;
        _shiftPeriodMs = period;
    }

    // 4-6. Higher-priority screens: ResetResult > OTA > Setup.
    if (_resetResultActive && !elapsed(now, _beginMs, DISPLAY_RESET_RESULT_MS)) {
        _holdActive = false;
        _override = Override::ResetResult;
        return makeView(DisplayScreen::ResetResult, 0, 0);
    }
    _resetResultActive = false;

    if (in.otaActive) {
        _holdActive = false;
        _override = Override::Ota;
        return makeView(DisplayScreen::Ota, 0, 0);
    }

    if (in.setupActive) {
        _holdActive = false;
        if (_override != Override::Setup) {
            _slotIndex = 0;
            _slotStartMs = now;
        } else if (elapsed(now, _slotStartMs, period)) {
            ++_slotIndex;
            _slotStartMs = now;
        }
        _override = Override::Setup;
        if (mask != 0 && (_slotIndex % DISPLAY_SETUP_ALARM_SLOT) == DISPLAY_SETUP_ALARM_SLOT - 1) {
            return makeView(DisplayScreen::Alarms, 0, subPageAt(now, _slotStartMs, in.alarmSubPages));
        }
        return makeView(DisplayScreen::Setup, 0, 0);
    }

    // 7. An override just ended: rotation restarts at page 0.
    if (_override != Override::None) {
        _override = Override::None;
        restartRotation(now, period);
    }

    // 8. Alarm jump + hold.
    if (rising != 0) {
        _holdActive = true;
        _holdStartMs = now;
    }
    if (_holdActive) {
        if (mask == 0 || elapsed(now, _holdStartMs, DISPLAY_ALARM_HOLD_MS)) {
            _holdActive = false;
            restartRotation(now, period);
        } else {
            return makeView(DisplayScreen::Alarms, 0, subPageAt(now, _holdStartMs, in.alarmSubPages));
        }
    }

    // 9. Rotation: controller pages, Network, Sensors, then Alarms while mask != 0.
    if (_onAlarmsInRotation && mask == 0) {
        restartRotation(now, period);
    }
    if (elapsed(now, _pageStartMs, _dwellMs)) {
        // uint8_t is safe: pageCount comes from DisplayPageList, capped at
        // DISPLAY_MAX_PAGES = 8 (static_assert in DisplayPages.h), so n never wraps to 0.
        const uint8_t n = static_cast<uint8_t>(_pageCount + (mask != 0 ? 1 : 0));
        _pageIndex = static_cast<uint8_t>((_pageIndex + 1) % n);
        _onAlarmsInRotation = _pageIndex == _pageCount;
        _pageStartMs = now;
        _dwellMs = _onAlarmsInRotation ? alarmsDwell(period, in.alarmSubPages) : period;
    }
    if (_onAlarmsInRotation) {
        return makeView(DisplayScreen::Alarms, 0, subPageAt(now, _pageStartMs, in.alarmSubPages));
    }
    return makeView(DisplayScreen::Page, _pageIndex, 0);
}
