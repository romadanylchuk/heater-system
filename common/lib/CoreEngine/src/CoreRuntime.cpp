#include "CoreRuntime.h"

#include <stdlib.h>
#include "BackupCodec.h"

namespace {

CommandStatus mapConfigStatus(ConfigStatus st) {
    switch (st) {
        case ConfigStatus::Ok:
            return CommandStatus::Ok;
        case ConfigStatus::Clamped:
            return CommandStatus::Clamped;
        case ConfigStatus::Unchanged:
            return CommandStatus::Unchanged;
        case ConfigStatus::InvalidIndex:
            return CommandStatus::InvalidCommand;
        case ConfigStatus::Rejected:
        case ConfigStatus::TypeMismatch:
        case ConfigStatus::StoreError:
        case ConfigStatus::SchemaInvalid:
        default:
            return CommandStatus::Rejected;
    }
}

}  // namespace

CoreRuntime::CoreRuntime(CommonState& state, ConfigEngine& config, EventLog& events, CommandQueue& queue)
    : _state(state), _config(config), _events(events), _queue(queue) {
    _state.eventLog = &events;
}

CommandStatus CoreRuntime::apply(Command& cmd, uint64_t monoMs) {
    CommandStatus status;

    switch (cmd.type) {
        case CommandType::SetNumber:
            status = mapConfigStatus(_config.setNumber(cmd.settingIndex, cmd.number, cmd.origin, monoMs));
            break;
        case CommandType::SetText:
            status = mapConfigStatus(_config.setText(cmd.settingIndex, cmd.text, cmd.origin, monoMs));
            break;
        case CommandType::ImportBackup:
            if (cmd.payload == nullptr) {
                status = CommandStatus::ImportFailed;
            } else {
                BackupImportResult result =
                    BackupCodec::importJson(_config, _events, cmd.payload, cmd.payloadLen, monoMs);
                status = result.status == BackupStatus::Ok ? CommandStatus::Ok : CommandStatus::ImportFailed;
            }
            break;
        case CommandType::FactoryReset:
            performFactoryReset(cmd.origin, monoMs);
            status = CommandStatus::Ok;
            break;
        case CommandType::None:
            status = CommandStatus::InvalidCommand;
            break;
        case CommandType::AssignSensor:
        case CommandType::ClearSensor:
        case CommandType::RescanOneWire:
        default:
            status = _extHandler != nullptr ? _extHandler(cmd, monoMs, _extCtx) : CommandStatus::InvalidCommand;
            break;
    }

    if (cmd.payload != nullptr) {
        free(cmd.payload);
        cmd.payload = nullptr;
    }

    _state.system.lastCommandId = cmd.id;
    _state.system.lastCommandStatus = static_cast<uint8_t>(status);
    return status;
}

void CoreRuntime::tick(uint64_t monoMs) {
    Command cmd;
    for (size_t i = 0; i < MAX_COMMANDS_PER_TICK; ++i) {
        if (!_queue.tryReceive(cmd)) {
            break;
        }
        apply(cmd, monoMs);
    }

    _config.tick(monoMs);

    _state.diag.nvsError = !_config.lastStoreOk() || !_events.lastPersistOk();
    _state.system.uptimeS = static_cast<uint32_t>(monoMs / 1000);
}

void CoreRuntime::performFactoryReset(EventReason origin, uint64_t monoMs) {
    uint16_t source = origin == EventReason::Di1 ? EVENT_SOURCE_DI1 : EVENT_SOURCE_WEB;
    // Logged first (D19 step 1), so the entry lands in the surviving log before the
    // config namespace (not the log namespace) is erased below.
    _events.logEvent(toU16(EventType::FactoryReset), source, 0, 0, origin);

    _config.factoryReset();

    _state.system.setupApRequested = true;

    if (_resetHook) {
        _resetHook(origin, _resetHookCtx);
    }

    // The DI1 path runs in setup() before any service starts, so it never reboots;
    // every other origin (web) requests a delayed reboot so the response goes out first.
    if (origin != EventReason::Di1) {
        _state.system.rebootRequested = true;
        _state.system.rebootAtMs = monoMs + REBOOT_DELAY_MS;
    }
}
