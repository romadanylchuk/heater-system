#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "EventTypes.h"
#include "SettingDescriptor.h"

// Fixed-size command POD posted from web/MQTT handlers (or boot logic) into the
// single-writer CoreRuntime via a CommandQueue (D13). The builders below are the
// only supported way to construct a Command.
enum class CommandType : uint8_t { None = 0, SetNumber, SetText, ImportBackup, FactoryReset };
enum class CommandStatus : uint8_t {
    Ok = 0,
    Clamped,
    Unchanged,
    Rejected,
    InvalidCommand,
    ImportFailed,
    QueueFull,
};

constexpr size_t COMMAND_TEXT_MAX = SETTING_TEXT_MAX_LEN;

struct Command {
    CommandType type = CommandType::None;
    EventReason origin = EventReason::None;
    uint16_t settingIndex = 0;
    uint32_t id = 0;
    float number = 0.0f;
    char text[COMMAND_TEXT_MAX + 1] = {};
    // ImportBackup only: malloc'd JSON payload. Ownership moves to the queue on a
    // successful post(); CoreRuntime::apply() always free()s it (D13).
    char* payload = nullptr;
    size_t payloadLen = 0;
};

inline Command makeSetNumber(uint16_t index, float value, EventReason origin, uint32_t id) {
    Command cmd;
    cmd.type = CommandType::SetNumber;
    cmd.origin = origin;
    cmd.settingIndex = index;
    cmd.id = id;
    cmd.number = value;
    return cmd;
}

// Copies text into the fixed command buffer. If it does not fit COMMAND_TEXT_MAX,
// the command comes back with type = None, so callers/consumers reject it as
// InvalidCommand rather than silently truncating it.
inline Command makeSetText(uint16_t index, const char* text, EventReason origin, uint32_t id) {
    Command cmd;
    cmd.origin = origin;
    cmd.settingIndex = index;
    cmd.id = id;
    size_t len = text != nullptr ? strlen(text) : 0;
    if (len > COMMAND_TEXT_MAX) {
        cmd.type = CommandType::None;
        return cmd;
    }
    cmd.type = CommandType::SetText;
    memcpy(cmd.text, text, len);
    cmd.text[len] = '\0';
    return cmd;
}

inline Command makeImportBackup(char* payload, size_t len, EventReason origin, uint32_t id) {
    Command cmd;
    cmd.type = CommandType::ImportBackup;
    cmd.origin = origin;
    cmd.id = id;
    cmd.payload = payload;
    cmd.payloadLen = len;
    return cmd;
}

inline Command makeFactoryReset(EventReason origin, uint32_t id) {
    Command cmd;
    cmd.type = CommandType::FactoryReset;
    cmd.origin = origin;
    cmd.id = id;
    return cmd;
}

// Non-blocking, fixed-size command channel between producers (web/MQTT handlers)
// and the single-writer CoreRuntime consumer. Implemented by FreeRtosCommandQueue
// (CoreEsp32) for firmware and InMemoryCommandQueue (common/test/fakes) natively.
class CommandQueue {
public:
    virtual ~CommandQueue() = default;

    // False if the queue is full; the caller keeps payload ownership in that case.
    virtual bool post(const Command& cmd) = 0;
    virtual bool tryReceive(Command& out) = 0;
};
