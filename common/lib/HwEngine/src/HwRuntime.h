#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <Command.h>
#include <ConfigEngine.h>
#include <EventTypes.h>
#include <HardwareStatus.h>
#include "AntiSeizeScheduler.h"
#include "HwConfig.h"
#include "K1Driver.h"
#include "LocalTime.h"
#include "OneWireBus.h"
#include "RelayBank.h"
#include "RelayPort.h"
#include "SensorService.h"

// Pure orchestration of the whole hardware service (D2): owns RelayBank,
// K1Driver, SensorService and AntiSeizeScheduler behind the RelayPort/
// OneWireBus interfaces, resolves the HW_SETTINGS + per-output enable keys by
// key (D3), replays K1Driver's desired power/direction into RelayBank every
// fast tick (the interlock at the output-byte level, K1Driver.h), and owns
// the relay port write policy (D13). HwEsp32's HardwareServices (stage 03
// phase 6) is glue only: it constructs the real port/bus and drives
// begin()/tick()/fastTick() from the main loop. CoreRuntime dispatches
// AssignSensor/ClearSensor/RescanOneWire to handleCommand() via commandHook
// (D22).
enum class HwStatus : uint8_t { Ok, InvalidConfig, SettingMissing };

constexpr uint32_t HW_FAST_TICK_MS = 100;
constexpr uint32_t RELAY_REASSERT_MS = 5000;

class HwRuntime {
public:
    HwRuntime(CommonState& state, ConfigEngine& config, EventSink& events, RelayPort& port, OneWireBus& bus);

    // validateHwConfig(cfg) else InvalidConfig; resolves the 4 HW_SETTINGS
    // keys and every anti-seize enableKey (must exist and be Bool) else
    // SettingMissing; configures RelayBank/K1Driver(if present)/SensorService
    // (false -> SettingMissing)/AntiSeizeScheduler; pushes the current
    // settings; sets state.k1.present; marks the runtime ready and fills the
    // initial status. On any failure the runtime stays not-ready:
    // fastTick() keeps writing the all-OFF byte and tick()/handleCommand()
    // are no-ops/Rejected.
    HwStatus begin(const HwProjectConfig& cfg, uint64_t nowMs);

    // AssignSensor/ClearSensor/RescanOneWire, dispatched here by CoreRuntime's
    // extension handler (D22). Anything else, or not ready, -> InvalidCommand
    // / Rejected.
    CommandStatus handleCommand(Command& cmd, uint64_t nowMs);

    // CommandExtensionHandler-shaped free function; ctx must be the HwRuntime*.
    static CommandStatus commandHook(Command& cmd, uint64_t monoMs, void* ctx);

    // ~1 s tick: pushes the current settings, ticks SensorService and
    // AntiSeizeScheduler, runs fastTick(), then fills state.sensors/oneWire/
    // alarms and state.antiSeize. No-op when not ready.
    void tick(uint64_t nowMs, const LocalTimeInfo& local);

    // ~100 ms tick (D12): ticks K1Driver and replays its desired power/
    // direction into RelayBank before RelayBank::update() (the interlock at
    // the output-byte level), writes the port on change or on the
    // RELAY_REASSERT_MS re-assert window (D13), and refreshes state.relays/
    // state.k1. Writes the all-OFF byte when not ready.
    void fastTick(uint64_t nowMs);

    RelayBank& relays() { return _relays; }
    K1Driver& k1() { return _k1; }
    SensorService& sensors() { return _sensors; }
    AntiSeizeScheduler& antiSeize() { return _antiSeize; }

    bool ready() const { return _ready; }

    // OTA output inhibit (stage 04, D13). Loop task only; the facade calls it
    // from the loop (including, for espota, synchronously from the espota
    // onStart callback on the loop task, followed by an immediate fastTick()).
    // Works whether or not the runtime is ready: the flag is always stored;
    // RelayBank::setInhibited() is only called when ready. true: cancels K1
    // (if present and ready) then inhibits RelayBank -- every actually-ON
    // channel switches OFF immediately. tick() then skips the anti-seize
    // scheduler (sensors keep running); fastTick() skips the K1 tick/replay
    // (cancelling any still-busy K1 run) and keeps writing the RelayBank
    // output byte (all-OFF while inhibited). false: releases RelayBank so
    // normal arbitration resumes at the next update().
    void setOutputsInhibited(bool inhibited, uint64_t nowMs);
    bool outputsInhibited() const { return _outputsInhibited; }

private:
    void pushSettings();
    void updateRelayAndK1Status(uint64_t nowMs);

    CommonState& _state;
    ConfigEngine& _config;
    EventSink& _events;
    RelayPort& _port;
    OneWireBus& _bus;

    RelayBank _relays;
    K1Driver _k1;
    SensorService _sensors;
    AntiSeizeScheduler _antiSeize;

    const HwProjectConfig* _cfg = nullptr;
    bool _ready = false;
    bool _outputsInhibited = false;

    size_t _idxLock = 0;
    size_t _idxInterval = 0;
    size_t _idxTime = 0;
    size_t _idxDuration = 0;
    size_t _idxEnable[MAX_ANTI_SEIZE_OUTPUTS] = {};

    uint8_t _lastWritten = 0;
    bool _hasWritten = false;
    uint64_t _lastWriteMs = 0;
    bool _ioError = false;
    uint32_t _ioErrorCount = 0;
};
