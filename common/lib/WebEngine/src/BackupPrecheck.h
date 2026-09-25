#pragma once
#include <stddef.h>
#include <stdint.h>

// Cheap pre-queue check of an uploaded backup file on the AsyncTCP task
// (D9): only "type"/"format" are inspected, so a wrong-controller or
// corrupt file is refused with a precise error before it is moved into the
// command queue. The full two-pass validation still runs in BackupCodec on
// the loop task.
enum class PrecheckResult : uint8_t { Ok, Empty, TooLarge, Corrupt, MissingType, WrongType, UnsupportedFormat };

// ArduinoJson filter parse (only "type"/"format" kept). gotType receives the
// file's type (truncated to cap-1, NUL-terminated; "" when absent). gotType
// may be null when cap == 0.
PrecheckResult precheckBackup(const char* json, size_t len, const char* expectedType, char* gotType, size_t cap);

// "ok", "empty", "too_large", "corrupt", "missing_type", "wrong_type", "unsupported_format".
const char* precheckKey(PrecheckResult r);
