#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ConfigEngine.h"
#include "EventTypes.h"

// JSON settings backup export/import (D18). Export writes every non-NO_BACKUP
// setting by logical key; import is two-pass, so a file that fails validation
// never causes a partial apply.
enum class BackupStatus : uint8_t {
    Ok = 0,
    Corrupt,
    MissingType,
    WrongType,
    UnsupportedFormat,
    InvalidValue,
    BufferTooSmall,
    NotReady,
};

struct BackupImportResult {
    BackupStatus status;
    uint16_t applied;
    uint16_t clamped;
    uint16_t ignoredUnknown;
    uint16_t backupVersion;
    bool newerVersion;
};

constexpr uint16_t BACKUP_FORMAT = 1;
constexpr size_t BACKUP_MAX_BYTES = 8192;

namespace BackupCodec {

// Writes JSON into out (NUL-terminated). Returns length, 0 if cap is too small or
// the engine is not ready.
size_t exportJson(const ConfigEngine& config, const char* fwVersion, char* out, size_t cap);

// Pass 1 validates the whole file (structure, type, format, configVersion, and
// every known key's JSON type/text length); any failure refuses the whole file
// with zero changes and logs BackupRejected. Pass 2 applies each present, known key
// through ConfigEngine::setNumber/setText (reason Import), then flushNow()s and
// logs BackupImported (plus BackupNewerVersion if the backup is from newer
// firmware).
BackupImportResult importJson(ConfigEngine& config, EventSink& events, const char* json, size_t len,
    uint64_t monoMs);

}  // namespace BackupCodec
