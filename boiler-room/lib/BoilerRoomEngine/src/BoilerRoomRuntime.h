#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <EventTypes.h>
#include <RelayBank.h>
#include "BoilerRoomController.h"
#include "BoilerRoomDiagnostics.h"
#include "BoilerRoomStatus.h"

// boiler-room controller runtime adapter (stage 07, C10). Pure: reads
// CommonState (sensors, network), ConfigEngine (settings + the persisted
// "Home: no need" flag, gated through haGatedFlag) and RelayBank (P3 actual /
// last-run); maps each pump decision onto the RelayBank slots (D3: safety slot
// only for the lock-bypass states, control slot underneath every tick); writes
// the BoilerRoomStatus slice and the controller alarm bits 0..23; self-clears
// the flag through ConfigEngine (D24) and logs the controller events (C9/D12).
// It never touches the exercise slot (anti-seize owns it) nor the OTA inhibit.
enum class BoilerRoomRuntimeStatus : uint8_t { Ok, SettingMissing };

// Keys the runtime resolves, in BoilerRoomSettings field order, then homeNoNeed.
enum class BrRuntimeKey : uint8_t {
    P1DeltaOn, P1DeltaOff, P1T1Min, P1Hyst, OhOn, OhClear, P2T2Off, P2Hyst, P2T1Burn, P3T3Offer,
    P3OfferWin, P3OfferWait, AfEnable, AfInterval, AfDuration, AccVolume, AccTBase, HomeNoNeed,
};
constexpr size_t BR_RUNTIME_KEY_COUNT = 18;
static_assert(static_cast<size_t>(BrRuntimeKey::HomeNoNeed) + 1 == BR_RUNTIME_KEY_COUNT,
    "BR_RUNTIME_KEY_COUNT must match the BrRuntimeKey enum");

// Diagnostics keys (stage 09, C9), in BOILER_ROOM_DIAG_SETTINGS table order.
// Resolved separately from BrRuntimeKey: a missing/mistyped diag key only
// disables the diagnostics, it never makes the runtime not-ready (D2).
enum class BrDiagKey : uint8_t {
    B1En, B1MinOn, B1Delta, B1MinRise, B3En, B3MinOn, B3Delta, B3MinRise, B6En, B6MinOn, B6Delta, B6MinRise,
};
constexpr size_t BR_DIAG_KEY_COUNT = 12;
static_assert(static_cast<size_t>(BrDiagKey::B6MinRise) + 1 == BR_DIAG_KEY_COUNT,
    "BR_DIAG_KEY_COUNT must match the BrDiagKey enum");

class BoilerRoomRuntime {
public:
    BoilerRoomRuntime(CommonState& state, BoilerRoomStatus& status, ConfigEngine& config, EventSink& events,
        RelayBank& relays);

    // Resolves every BR_KEY_* + "homeNoNeed" by ConfigEngine::indexOf (type-checked); stores bootMs.
    BoilerRoomRuntimeStatus begin(uint64_t nowMs);

    // ~1 s, loop task, AFTER hw.tick() and BEFORE net.tick(). See Phase 4 for the exact sequence.
    void tick(uint64_t nowMs);

    bool ready() const { return _ready; }
    bool diagReady() const { return _diagReady; }

    // Reads the raw (unguarded) settings; idx[] is indexed by BrRuntimeKey.
    static BoilerRoomSettings readSettings(const ConfigEngine& c, const size_t idx[]);

    // Reads the diagnostics settings; idx[] is indexed by BrDiagKey.
    static BoilerRoomDiagSettings readDiagSettings(const ConfigEngine& c, const size_t idx[]);

private:
    CommonState& _state;
    BoilerRoomStatus& _status;
    ConfigEngine& _config;
    EventSink& _events;
    RelayBank& _relays;

    size_t _idx[BR_RUNTIME_KEY_COUNT] = {};
    bool _ready = false;
    bool _missingLogged = false;   // settings-missing DiagnosticWarning: once per boot
    uint64_t _bootMs = 0;
    BoilerRoomController _controller;

    // Edge tracking for the event log.
    P3Mode _prevMode = P3Mode::Normal;
    uint32_t _prevMask = 0;
    bool _prevOn[BR_PUMP_COUNT] = {};
    bool _prevSafety[BR_PUMP_COUNT] = {};

    // Pump-response diagnostics (stage 09, C9): warnings only, never a control input (D2).
    BoilerRoomDiagnostics _diag;
    bool _diagReady = false;
    size_t _diagIdx[BR_DIAG_KEY_COUNT] = {};
    uint32_t _prevWarn = 0;   // owned warning bits last published (D6)

    void logEvent(uint16_t type, uint16_t source, float value, float aux);
    bool resolveDiagKeys();
    void tickDiagnostics(const BoilerRoomInputs& in, uint64_t nowMs);
    void publishWarnings(uint32_t mask);
};
