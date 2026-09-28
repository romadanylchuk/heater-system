#pragma once
#include <stdint.h>
#include <EventTypes.h>

// boiler-room project event types (stage 07, C9/D12). Everything else reuses the
// common catalogue: AlarmRaised/AlarmCleared (source EVENT_SOURCE_ALARM_BASE +
// bit, value = T1 for bit 0 else 0, reason Logic), AntiFreezeRun (source
// EVENT_SOURCE_RELAY_BASE + 2, value = afDurationS) and the flag self-clear
// logged by ConfigEngine itself as ConfigChanged with reason Logic.
//
// BR_EVENT_P3_MODE: value = new P3Mode, aux = old P3Mode. Source =
//   EVENT_SOURCE_PROJECT_BASE + (uint16_t)newMode -- distinct per target mode,
//   so the 60 s (type, source) limiter never swallows OFF->OFFER right after
//   NORMAL->OFF.
// BR_EVENT_PUMP_REQUEST: value = PumpReason code, aux = on (0/1). Source =
//   EVENT_SOURCE_RELAY_BASE + channel. Logged only when a pump's requested `on`
//   or `safety` flips (not on a reason-only change).
constexpr uint16_t BR_EVENT_P3_MODE = EVENT_TYPE_PROJECT_BASE + 0;
constexpr uint16_t BR_EVENT_PUMP_REQUEST = EVENT_TYPE_PROJECT_BASE + 1;

// DiagnosticWarning code for "a controller setting key is missing / mistyped"
// (D17, final-check Should 5): source = EVENT_SOURCE_DIAG_BASE + this code,
// value = BrRuntimeKey index that failed to resolve, reason Logic. Logged once
// per boot by BoilerRoomRuntime::begin(). Project-owned: the common codes 1..6
// live in HardwareStatus.h; 32+ is reserved for boiler-room.
constexpr uint16_t BR_DIAG_CODE_SETTING_MISSING = 32;

// Stage 09 (C9, D7): a pump-response diagnostic warning (B1/B3/B6) was raised or
// cleared. value = warning bit (BR_WARN_*), aux = 1 raised / 0 cleared, reason
// Logic. Source = BR_EVENT_SOURCE_DIAG_BASE + bit*2 + raised -- distinct per
// check AND direction, so the 60 s (type, source) limiter never swallows a
// raise/clear pair (A5). 0x40 is unused by the other boiler-room sources.
constexpr uint16_t BR_EVENT_DIAG_WARNING = EVENT_TYPE_PROJECT_BASE + 2;
constexpr uint16_t BR_EVENT_SOURCE_DIAG_BASE = EVENT_SOURCE_PROJECT_BASE + 0x40;

// DiagnosticWarning code for "a diagnostics setting key is missing / mistyped"
// (D2): source = EVENT_SOURCE_DIAG_BASE + this code, value = BrDiagKey index.
// Only the diagnostics are disabled; control readiness is unaffected.
constexpr uint16_t BR_DIAG_CODE_DIAG_SETTING_MISSING = 33;
