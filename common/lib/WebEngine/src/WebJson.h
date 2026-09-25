#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Command.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <EventLog.h>
#include <HwConfig.h>
#include "CommandResultBoard.h"
#include "JsonOut.h"

// JSON builders for the stage-05 read API (D8). All run on the loop task into
// caller-owned buffers through JsonOut (no heap). Every builder returns the
// output length, or 0 on overflow; the buffer stays NUL-terminated either
// way. Shapes are the SPA contract (feature-plan "JSON shapes").

// Formats a UTC epoch as controller-local "YYYY-MM-DD HH:MM:SS"; false if it
// cannot (the field is then written as "").
using LocalTimeFormatter = bool (*)(uint32_t utc, char* out, size_t cap, void* ctx);
// Writes the value of "ctl" (stages 07/08, D22); false aborts the build (0).
using StateJsonExtension = bool (*)(const CommonState& s, JsonOut& out, void* ctx);

struct WebJsonContext {
    const char* project;
    const HwProjectConfig* hw;
    LocalTimeFormatter fmt;
    void* fmtCtx;
    StateJsonExtension ext;   // null in stage 05
    void* extCtx;
};

size_t buildStateJson(const CommonState& s, const WebJsonContext& c, char* out, size_t cap);
size_t buildSensorsJson(const CommonState& s, const HwProjectConfig& hw, char* out, size_t cap);
// SECRET values never appear in "v"; "s" only says whether each is set.
size_t buildConfigValuesJson(const ConfigEngine& cfg, char* out, size_t cap);
// /api/log sizing (review-4 Must-fix). WEB_LOG_ENTRY_MAX bounds one rendered
// entry (incl. its separating comma) for: 10-digit seq/ts, 5-digit type/src,
// a key of <= WEB_LOG_KEY_MAX chars, |val|,|aux| < 1e6 with 2 decimals
// ("-999999.99"), 3-digit rsn, "rt":false and an "lt" of up to 31 chars.
// Entries outside that bound (e.g. huge floats) are still handled: the
// builder truncates instead of failing (see buildLogJson).
constexpr size_t WEB_LOG_KEY_MAX = 24;
constexpr size_t WEB_LOG_ENTRY_MAX = 200;
// {"events":[ + ],"truncated":true} + NUL, rounded up.
constexpr size_t WEB_LOG_FRAME = 40;
constexpr size_t webLogCapFor(size_t entries) {
    return entries * WEB_LOG_ENTRY_MAX + WEB_LOG_FRAME;
}

// Newest first. Entries without EVENT_FLAG_REAL_TIME get "lt":"".
// Degrades gracefully: when the next (older) entry would not fit, it and all
// older ones are dropped and the document ends with "truncated":true (the
// member is absent when every entry fits). The result is always valid JSON;
// 0 only if not even the empty frame fits.
size_t buildLogJson(const EventLog& log, const WebJsonContext& c, char* out, size_t cap);
size_t buildCmdResultJson(CmdLookup l, const CmdResult& r, uint32_t id, char* out, size_t cap);

// ok, clamped, unchanged, rejected, invalid, import_failed, queue_full.
const char* commandStatusKey(CommandStatus st);
