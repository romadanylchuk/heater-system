#pragma once
#include <stddef.h>
#include <stdint.h>
#include <AntiSeizeScheduler.h>
#include <Command.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <EventTypes.h>
#include <K1Driver.h>
#include <LocalTime.h>
#include <RelayBank.h>
#include "HomeHeatingController.h"
#include "HomeHeatingDiagSettings.h"
#include "HomeHeatingDiagnostics.h"
#include "HomeHeatingStatus.h"
#include "K1PulseCounter.h"
#include "K1StepTest.h"

// home-heating controller runtime adapter (stage 08, C12). Pure: reads
// CommonState (sensors, anti-seize status), ConfigEngine (settings + the
// existing "heatingEnabled" row), RelayBank (P4/K2 actual, OTA inhibit) and
// K1Driver (busy/owner/motion); drives P4 and K2 ONLY through the RelayBank
// control slot (60 s lock, A5/D3 -- never the safety slot, never the exercise
// slot, never the inhibit), drives K1 ONLY through
// K1Driver::requestPulse(..., K1Owner::Control), is the sole caller of
// K1Driver::takeMotion() (D6), pushes the anti-seize K1 stroke length (D16),
// writes the HomeHeatingStatus slice, owns alarm bits 0..23 and logs the
// controller events (C11).
enum class HomeHeatingRuntimeStatus : uint8_t { Ok, SettingMissing };

// Keys the runtime resolves, in HomeHeatingSettings field order, then heatingEnabled.
enum class HhRuntimeKey : uint8_t {
    H2Set, P4OffDelay, K1Travel, K1Period, K1Deadband, K1Gain, K1MaxPulse, K1MinPulse, K1Resync, K1SmallDiff,
    K1FailPos, K1FfStep, K2Delta, K2DeltaHyst, K2H3Min, K2H3MinHyst, K2H4Max, K2H4MaxHyst, HeatingEnabled,
};
constexpr size_t HH_RUNTIME_KEY_COUNT = 19;
static_assert(static_cast<size_t>(HhRuntimeKey::HeatingEnabled) + 1 == HH_RUNTIME_KEY_COUNT,
    "HH_RUNTIME_KEY_COUNT must match the HhRuntimeKey enum");

// Stage 09 (C15, D2): the diagnostics keys, in HOME_HEATING_DIAG_SETTINGS table
// order. Resolved separately from the control keys: a failure only disables the
// diagnostics and the step test, never the controller.
enum class HhDiagKey : uint8_t { H1En, H1MinOn, H1K1Min, H1Delta, H1MinDiff, K1StepPulse };
constexpr size_t HH_DIAG_KEY_COUNT = 6;
static_assert(static_cast<size_t>(HhDiagKey::K1StepPulse) + 1 == HH_DIAG_KEY_COUNT,
    "HH_DIAG_KEY_COUNT must match the HhDiagKey enum");

class HomeHeatingRuntime {
public:
    HomeHeatingRuntime(CommonState& state, HomeHeatingStatus& status, ConfigEngine& config, EventSink& events,
        RelayBank& relays, K1Driver& k1, AntiSeizeScheduler& antiSeize);

    // Resolves every HH_KEY_* + "heatingEnabled" by ConfigEngine::indexOf, type-checked
    // against the C2 table (Bool for heatingEnabled); DiagnosticWarning once per boot on
    // failure (D20); controller.reset(nowMs) on success.
    HomeHeatingRuntimeStatus begin(uint64_t nowMs);

    // ~1 s, loop task, AFTER hw.tick() and BEFORE net.tick() (D24). `local` is the
    // local wall-clock day for the K1 pulse counter rollover (stage 09, D10).
    void tick(uint64_t nowMs, const LocalTimeInfo& local);
    void tick(uint64_t nowMs) { tick(nowMs, NO_LOCAL_TIME); }

    // Stage 09 (C3, C15, D15): project command ops HH_CMD_STEP_START / HH_CMD_STEP_CANCEL.
    // Loop task, inside core.tick. START is validated against the last tick's
    // status.step.block and latched; the next tick re-validates (a start that became
    // blocked meanwhile is dropped). A second START while running/pending -> Rejected;
    // CANCEL when idle -> Unchanged; other types/ops -> InvalidCommand.
    CommandStatus handleCommand(const Command& cmd, uint64_t nowMs);
    // CommandExtensionHandler shape; ctx is the HomeHeatingRuntime.
    static CommandStatus commandHook(Command& cmd, uint64_t monoMs, void* ctx);

    bool ready() const { return _ready; }
    bool diagReady() const { return _diagReady; }

    // Last value pushed to AntiSeizeScheduler::setK1StrokeMs (0 before the first ready tick).
    uint32_t k1StrokeMs() const { return _lastStrokeMs; }

    // Reads the raw (unguarded) settings; idx[] is indexed by HhRuntimeKey.
    static HomeHeatingSettings readSettings(const ConfigEngine& c, const size_t idx[]);

    // Reads the diagnostics settings; idx[] is indexed by HhDiagKey.
    static HomeHeatingDiagSettings readDiagSettings(const ConfigEngine& c, const size_t idx[]);

private:
    CommonState& _state;
    HomeHeatingStatus& _status;
    ConfigEngine& _config;
    EventSink& _events;
    RelayBank& _relays;
    K1Driver& _k1;
    AntiSeizeScheduler& _antiSeize;

    size_t _idx[HH_RUNTIME_KEY_COUNT] = {};
    bool _ready = false;
    bool _missingLogged = false;   // settings-missing DiagnosticWarning: once per boot
    uint32_t _lastStrokeMs = 0;
    HomeHeatingController _controller;

    // Edge tracking for the event log.
    bool _prevP4On = false;
    bool _prevK2Bypass = false;
    FailMode _prevFail = FailMode::None;
    bool _prevNoNeed = false;
    uint32_t _prevMask = 0;

    // Stage 09 (C15). Diagnostics are isolated from control (D2): they run after
    // the outputs are applied and only publish diag.warningMask bits 0..7 (D6).
    HomeHeatingDiagnostics _diag;
    K1StepTest _step;
    K1PulseCounter _pulses;
    bool _diagReady = false;
    size_t _diagIdx[HH_DIAG_KEY_COUNT] = {};
    uint32_t _prevWarn = 0;
    bool _startPending = false;    // project command latches (C15)
    bool _cancelPending = false;
    int8_t _lastPulseDir = 0;      // D11: FF / feedback / step-test pulses only
    uint32_t _lastPulseMs = 0;

    bool resolveDiagKeys();
    void publishWarnings(uint32_t mask);
    void tickDiagnostics(const HomeHeatingInputs& in, const HomeHeatingOutputs& out, uint64_t nowMs);
    StepTestOutput tickStepTest(const HomeHeatingInputs& in, const HomeHeatingOutputs& out,
        const HomeHeatingSettings& raw, uint64_t nowMs);
    void logEvent(uint16_t type, uint16_t source, float value, float aux);
    void writeStatus(const HomeHeatingSettings& raw, const HomeHeatingInputs& in, const HomeHeatingOutputs& out,
        const StepTestOutput& so, uint64_t nowMs);
};
