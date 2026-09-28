#pragma once
#include <stddef.h>
#include <stdint.h>
#include <AntiSeizeScheduler.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <EventTypes.h>
#include <K1Driver.h>
#include <RelayBank.h>
#include "HomeHeatingController.h"
#include "HomeHeatingStatus.h"

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

class HomeHeatingRuntime {
public:
    HomeHeatingRuntime(CommonState& state, HomeHeatingStatus& status, ConfigEngine& config, EventSink& events,
        RelayBank& relays, K1Driver& k1, AntiSeizeScheduler& antiSeize);

    // Resolves every HH_KEY_* + "heatingEnabled" by ConfigEngine::indexOf, type-checked
    // against the C2 table (Bool for heatingEnabled); DiagnosticWarning once per boot on
    // failure (D20); controller.reset(nowMs) on success.
    HomeHeatingRuntimeStatus begin(uint64_t nowMs);

    // ~1 s, loop task, AFTER hw.tick() and BEFORE net.tick() (D24).
    void tick(uint64_t nowMs);

    bool ready() const { return _ready; }

    // Last value pushed to AntiSeizeScheduler::setK1StrokeMs (0 before the first ready tick).
    uint32_t k1StrokeMs() const { return _lastStrokeMs; }

    // Reads the raw (unguarded) settings; idx[] is indexed by HhRuntimeKey.
    static HomeHeatingSettings readSettings(const ConfigEngine& c, const size_t idx[]);

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

    void logEvent(uint16_t type, uint16_t source, float value, float aux);
    void writeStatus(const HomeHeatingSettings& raw, const HomeHeatingOutputs& out, uint64_t nowMs);
};
