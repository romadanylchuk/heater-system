#pragma once
#include <stdint.h>
#include <SensorHistory.h>
#include "HomeHeatingTypes.h"
#include "K1Logic.h"

// K1 step test (stage 09, C14, A10, D13, D15-D19). Pure; one update() per
// tick, time injected. The runtime evaluates it after the controller on the
// same tick's K1Decision. A start issues one OPEN pulse of pulseS seconds and
// then watches H2: the dead time is the first tick with |H2 - H2start| >= 0.5,
// the settled value is reached when H2 stays within a 0.2 degC window for
// 120 s (or at 600 s). R = settled rise per second of pulse gives the
// suggested k1Gain / k1Period (D18). RAM only: last() is kept until the next
// test ends.

// Fixed constants (A6: not settings).
constexpr uint16_t STEP_STEADY_SAMPLES = 11;        // t-300 s .. t at 30 s (D16)
constexpr float STEP_H3_BAND_C = 1.0f, STEP_H2_BAND_C = 0.5f;
constexpr float STEP_MOVE_C = 0.5f;                  // dead-time threshold
constexpr float STEP_SETTLE_C = 0.2f;
constexpr uint32_t STEP_SETTLE_MS = 120000;
constexpr uint32_t STEP_OBSERVE_MS = 600000;         // 10 min
constexpr float STEP_H3_ABORT_C = 2.0f;
constexpr float STEP_GAIN_FRACTION = 0.5f;           // D18
constexpr float STEP_PERIOD_FACTOR = 1.5f;

// Stable codes (event values / JSON keys): never renumber.
enum class StepBlock : uint8_t {
    None = 0, Unavailable, Running, HeatingOff, P4Off, Ota, AntiSeize, Sensors,
    K1Mode, K1Unknown, K1Busy, K1Headroom, History, H3Unsteady, H2Unsteady,
};
enum class StepAbort : uint8_t {
    None = 0, Cancel, HeatingOff, P4Off, Ota, AntiSeize, Recal, Sensors, K1Mode, H3Changed,
};
enum class StepOutcome : uint8_t { None = 0, Result, NoResponse, Aborted };

// "none","unavailable","running","heating_off","p4_off","ota","anti_seize",
// "sensors","k1_mode","k1_unknown","k1_busy","k1_headroom","history",
// "h3_unsteady","h2_unsteady"
const char* stepBlockKey(StepBlock b);
// "none","cancel","heating_off","p4_off","ota","anti_seize","recal","sensors",
// "k1_mode","h3_changed"
const char* stepAbortKey(StepAbort a);
// "none","result","no_response","aborted"
const char* stepOutcomeKey(StepOutcome o);

struct StepSteadiness {
    bool full;
    bool h3Steady;
    bool h2Steady;
};

// full = hist.size() >= STEP_STEADY_SAMPLES; h3/h2Steady = hist.steady(sensor,
// live value, band, STEP_STEADY_SAMPLES) with H3 +-1 degC, H2 +-0.5 degC (D16).
StepSteadiness stepSteadiness(const SensorHistory& hist, float h2Now, float h3Now);

struct StepTestInputs {
    bool available;                 // runtime diag settings resolved
    bool startRequest, cancelRequest;
    bool heatingEnabled, p4Requested, p4RelayActual, inhibited, k1AntiSeizeOwned, k1Busy, k1CmdThisTick;
    K1Mode k1Mode;                  // this tick's K1Decision
    bool k1Known;
    float k1PosPct;
    bool recalStarted;
    SensorHealth h1, h2, h3;
    float h2C, h3C;
    StepSteadiness steady;
};

struct StepTestSettings {
    uint32_t pulseS;                // k1StepPulse
    uint32_t travelS;               // guarded k1Travel
};

struct StepSuggestion {
    bool valid;
    uint32_t periodS;
    float gain;
};

struct StepTestResult {
    StepOutcome outcome;
    StepAbort abort;
    float deadTimeS, responseCps;   // valid for Result (deadTimeS also for NoResponse-negative)
    float h2StartC, h2SettledC;
    uint32_t pulseS;
    StepSuggestion suggest;
};

struct StepTestOutput {
    K1Command pulse;                // issue only on the start tick: OPEN, pulseS*1000 ms
    bool started, ended;            // one-tick edges (ended = Result | NoResponse | Aborted)
    StepBlock block;                // first failing start precondition (None when startable)
};

// D18: periodS = round(1.5 * deadTimeS) clamped to the k1Period row range;
// gain = round-to-0.5(0.5 / responseCps) clamped to the k1Gain row range
// (ranges read from HOME_HEATING_CONTROL_SETTINGS by key). valid = false for
// deadTimeS <= 0, responseCps <= 0 or either non-finite.
StepSuggestion suggestK1Tuning(float deadTimeS, float responseCps);

class K1StepTest {
public:
    void reset();                    // idle, last.outcome = None
    // Per tick: 1. block evaluation; 2. running: abort check; 3. running:
    // measure; 4. idle + startRequest + block None: start (C14).
    StepTestOutput update(const StepTestInputs& in, const StepTestSettings& s, uint64_t nowMs);
    bool running() const { return _running; }
    uint32_t elapsedS(uint64_t nowMs) const;   // 0 when idle
    bool deadTimeSeen() const { return _running && _deadSeen; }
    float deadTimeS() const;                    // live while running (0 when not seen)
    uint32_t pulseS() const { return _pulseS; } // pulse of the running/last test
    const StepTestResult& last() const { return _last; }

private:
    StepBlock evaluateBlock(const StepTestInputs& in, const StepTestSettings& s) const;
    StepAbort abortReason(const StepTestInputs& in) const;
    void finish(StepOutcome outcome, StepAbort abort, float h2SettledC, StepSuggestion suggest,
        float responseCps);

    bool _running = false;
    uint64_t _t0Ms = 0;
    float _h2StartC = 0.0f;
    float _h3StartC = 0.0f;
    uint32_t _pulseS = 0;
    bool _deadSeen = false;
    uint32_t _deadMs = 0;
    uint64_t _winStartMs = 0;
    float _winMin = 0.0f;
    float _winMax = 0.0f;
    StepTestResult _last{};
};
