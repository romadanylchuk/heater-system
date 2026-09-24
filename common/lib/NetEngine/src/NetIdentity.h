#pragma once
#include <stddef.h>

// Per-project identity constants (stage 04): the MQTT/mDNS/discovery prefix,
// the HA unique-id prefix, and the human-facing device name/model/setup-AP
// SSID. Each project declares one `NetIdentity` (see boiler-room/src/
// BoilerRoomNet.h, home-heating/src/HomeHeatingNet.h) and passes it to
// ConnectivityRuntime. Pure data + validation; no allocation, no I/O.
constexpr size_t NET_PREFIX_MAX_LEN = 24;

struct NetIdentity {
    const char* prefix;        // "boiler-room": MQTT prefix, mDNS/DHCP hostname, discovery node id
    const char* uniquePrefix;  // "boiler_room": unique_id prefix, HA device identifier
    const char* deviceName;    // "Boiler room"
    const char* model;         // "KC868-A6 boiler-room"
    const char* apSsid;        // "BoilerRoom-Setup"
};

// All fields non-null/non-empty. `prefix`: 1..NET_PREFIX_MAX_LEN chars of
// [a-z0-9-], and must not be exactly "boiler" or "home" (those are the
// generic reference-project names, never a real project identity).
// `uniquePrefix`: 1..NET_PREFIX_MAX_LEN chars of [a-z0-9_]. `apSsid`: 1..32
// chars. `deviceName`/`model` are only checked for non-empty.
bool validateNetIdentity(const NetIdentity& id);

// Converts a camelCase/space/dash-separated key into a snake_case MQTT/HA
// key: "relayLock" -> "relay_lock", "K1 power" -> "k1_power", "T1" -> "t1".
// An uppercase letter that follows a lowercase letter or digit gets a '_'
// inserted before it; space and '-' become '_'; every letter is lowercased;
// only [a-z0-9_] characters are kept (anything else is dropped). Returns
// false if the result would be empty, or if it (including the terminating
// NUL) does not fit in `cap` bytes -- `out` is left untouched in that case.
bool toSnakeKey(const char* in, char* out, size_t cap);
