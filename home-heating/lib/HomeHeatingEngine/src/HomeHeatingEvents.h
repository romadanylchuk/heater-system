#pragma once
#include <stdint.h>
#include <EventTypes.h>

// home-heating project event types (stage 08, C11/D18). All logged with reason
// Logic. Everything else reuses the common catalogue: AlarmRaised/AlarmCleared
// (source EVENT_SOURCE_ALARM_BASE + bit, value 0). No per-pulse K1 events.
//
// HH_EVENT_P4_REQUEST: value = P4Reason, aux = on (0/1); source RELAY_BASE + 3.
// HH_EVENT_K2_REQUEST: value = K2Reason, aux = bypass (0/1); source RELAY_BASE + 0.
//   Both only when the requested on/bypass flips (never on a reason-only change).
// HH_EVENT_FAILSAFE:   value = new FailMode, aux = old; source PROJECT_BASE + 0x10 + new.
// HH_EVENT_K1_RECAL:   value 1 start / 0 end; source PROJECT_BASE + 0x20 + value.
// HH_EVENT_NO_NEED:    value = new 0/1; source PROJECT_BASE + 0x30 + value.
// Sources are distinct per target value so the 60 s (type, source) limiter never
// swallows an ON->OFF pair.
constexpr uint16_t HH_EVENT_P4_REQUEST = EVENT_TYPE_PROJECT_BASE + 0;
constexpr uint16_t HH_EVENT_K2_REQUEST = EVENT_TYPE_PROJECT_BASE + 1;
constexpr uint16_t HH_EVENT_FAILSAFE = EVENT_TYPE_PROJECT_BASE + 2;
constexpr uint16_t HH_EVENT_K1_RECAL = EVENT_TYPE_PROJECT_BASE + 3;
constexpr uint16_t HH_EVENT_NO_NEED = EVENT_TYPE_PROJECT_BASE + 4;

constexpr uint16_t HH_EVENT_SOURCE_FAILSAFE_BASE = EVENT_SOURCE_PROJECT_BASE + 0x10;
constexpr uint16_t HH_EVENT_SOURCE_K1_RECAL_BASE = EVENT_SOURCE_PROJECT_BASE + 0x20;
constexpr uint16_t HH_EVENT_SOURCE_NO_NEED_BASE = EVENT_SOURCE_PROJECT_BASE + 0x30;

// DiagnosticWarning code for "a controller setting key is missing / mistyped"
// (D20): source = EVENT_SOURCE_DIAG_BASE + this code, value = HhRuntimeKey index
// that failed to resolve, reason Logic. Logged once per boot by
// HomeHeatingRuntime::begin(). Project-owned (32+ reserved per firmware).
constexpr uint16_t HH_DIAG_CODE_SETTING_MISSING = 32;

// ---- stage 09 (C15, D7) --------------------------------------------------------
// HH_EVENT_DIAG_WARNING: value = warn bit, aux = 1 raised / 0 cleared. Source =
//   HH_EVENT_SOURCE_DIAG_BASE + bit*2 + raised -- distinct per check and direction
//   so the 60 s (type, source) limiter never swallows a raise/clear pair (D7).
//   0x40 is unused by the other home-heating sources (0x10 / 0x20 / 0x30).
// HH_EVENT_STEP_START:  value = pulse s, aux = H2 at start.
// HH_EVENT_STEP_RESULT: value = dead time s (-1 for NoResponse without movement),
//   aux = response degC/s (0 for NoResponse).
// HH_EVENT_STEP_ABORT:  value = StepAbort code, aux = elapsed s.
// The step events use HH_EVENT_SOURCE_STEP_BASE + 0 start / 1 result / 2 abort.
constexpr uint16_t HH_EVENT_DIAG_WARNING = EVENT_TYPE_PROJECT_BASE + 5;
constexpr uint16_t HH_EVENT_STEP_START = EVENT_TYPE_PROJECT_BASE + 6;
constexpr uint16_t HH_EVENT_STEP_RESULT = EVENT_TYPE_PROJECT_BASE + 7;
constexpr uint16_t HH_EVENT_STEP_ABORT = EVENT_TYPE_PROJECT_BASE + 8;

constexpr uint16_t HH_EVENT_SOURCE_DIAG_BASE = EVENT_SOURCE_PROJECT_BASE + 0x40;
constexpr uint16_t HH_EVENT_SOURCE_STEP_BASE = EVENT_SOURCE_PROJECT_BASE + 0x50;

// DiagnosticWarning code for "a diagnostics setting key is missing / mistyped"
// (D2): source = EVENT_SOURCE_DIAG_BASE + this code, value = HhDiagKey index.
// Diagnostics + step test are disabled; the controller stays ready.
constexpr uint16_t HH_DIAG_CODE_DIAG_SETTING_MISSING = 33;

// Project command ops (C3/C4, POST /api/project/cmd).
constexpr uint8_t HH_CMD_STEP_START = 1;
constexpr uint8_t HH_CMD_STEP_CANCEL = 2;
