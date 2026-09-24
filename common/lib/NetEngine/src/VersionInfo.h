#pragma once
#include <stddef.h>
#include <CommonState.h>

// Web (LittleFS-bundled) firmware version text parsing and the
// fw/web version-mismatch status builder (D19).

// Trims leading/trailing whitespace (space, tab) and CR/LF from `raw[0..len)`,
// rejects an empty or non-printable (outside 0x20..0x7E) result, and copies
// the trimmed text into `out`, truncated to fit `cap` (including the NUL).
// Returns false (and leaves `out` untouched) when the trimmed text is empty
// or contains a non-printable byte.
bool parseWebVersionText(const char* raw, size_t len, char* out, size_t cap);

// Fills `out` from the firmware version string `fw` and the (possibly null)
// web version string `web`, copying with truncation to each field's capacity
// (a null pointer copies as ""). `webMismatch` is set when `web` is non-empty
// and differs from `fw`.
void fillVersionStatus(VersionStatus& out, const char* fw, const char* web);
