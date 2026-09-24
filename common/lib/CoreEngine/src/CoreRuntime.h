#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Command.h"
#include "CommonState.h"
#include "ConfigEngine.h"
#include "EventLog.h"

// Single-writer command consumer (D13): drains the CommandQueue, applies each
// command to ConfigEngine/BackupCodec, ticks the settings debounce, and owns
// factory-reset orchestration (D19). One instance per controller, driven from the
// ~1 s main loop.
using FactoryResetHook = void (*)(EventReason origin, void* ctx);

class CoreRuntime {
public:
    static constexpr size_t MAX_COMMANDS_PER_TICK = 16;
    static constexpr uint32_t REBOOT_DELAY_MS = 1000;

    // Sets state.eventLog = &events (D21).
    CoreRuntime(CommonState& state, ConfigEngine& config, EventLog& events, CommandQueue& queue);

    // Drains up to MAX_COMMANDS_PER_TICK commands, ticks the config debounce, and
    // refreshes state.diag.nvsError / state.system.uptimeS.
    void tick(uint64_t monoMs);

    // Applies one command. Always free()s cmd.payload (if non-null) and nulls it,
    // regardless of outcome, and updates state.system.lastCommandId/lastCommandStatus.
    CommandStatus apply(Command& cmd, uint64_t monoMs);

    // Runs the ordered factory-reset sequence (D19): logs FactoryReset first (so it
    // survives the erase), resets the config store to defaults, requests the setup
    // AP, calls the optional hook, and (for any origin but Di1) requests a reboot
    // REBOOT_DELAY_MS later.
    void performFactoryReset(EventReason origin, uint64_t monoMs);

    void setFactoryResetHook(FactoryResetHook hook, void* ctx) {
        _resetHook = hook;
        _resetHookCtx = ctx;
    }

private:
    CommonState& _state;
    ConfigEngine& _config;
    EventLog& _events;
    CommandQueue& _queue;

    FactoryResetHook _resetHook = nullptr;
    void* _resetHookCtx = nullptr;
};
