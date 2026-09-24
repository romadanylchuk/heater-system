#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <KvStore.h>
#include "OtaPlatform.h"

// OTA boot classification, marker persistence and post-boot health
// confirmation (D15). Pure: no esp_ota_ops.h/NVS -- OtaPlatform/KvStore are
// injected ports. The caller (NetEsp32's facade / ConnectivityRuntime, a
// later phase) reads OtaPlatform + the marker at boot, calls
// classifyOtaBoot(), then drives OtaHealthMonitor from CommonState.oneWire
// .readCycleCount every slow tick while a Trial is pending.
constexpr const char* NVS_NS_OTA = "ota";     // KvStore namespace the adapter opens for the marker
constexpr const char* OTA_MARKER_KEY = "target";

// Snapshot of the boot-time OTA platform state used to classify the boot.
struct OtaBootInfo {
    OtaImageState imageState;    // the *running* partition's validation state
    const char* runningLabel;    // the running partition's label
    bool markerPresent;          // whether an OTA marker was found in NVS
    const char* markerTarget;    // the marker's stored label, valid only if markerPresent
};

// D15's boot-classification table:
//   running == marker && pending      -> Trial
//   running == marker && !pending     -> UpdatedNoRollback
//   marker present && running != marker -> RolledBack
//   !marker present && pending        -> Trial
//   otherwise                          -> Normal
// Pure classification only: clearing the marker and logging OtaRollback for
// the RolledBack case, and UpdatedNoRollback's marker clear, are the
// caller's responsibility (it also owns the KvStore/EventSink).
OtaBootOutcome classifyOtaBoot(const OtaBootInfo& info);

// Thin, testable wrapper over a KvStore bound to the "ota" namespace, storing
// the label of the partition a successful update will boot next.
class OtaMarkerStore {
public:
    explicit OtaMarkerStore(KvStore& store) : _store(store) {}

    // False if absent or on a read error. out left untouched on false.
    bool load(char* out, size_t cap) const;

    // setStr + commit.
    bool save(const char* label);

    // remove + commit; a NotFound remove is not an error.
    bool clear();

private:
    KvStore& _store;
};

// Post-boot health confirmation for a Trial boot (D15's "Addition"). Health
// requires all three: >= CONFIRM_AFTER_MS since begin(), >= MIN_TICKS calls
// to tick(), and >= MIN_SENSOR_CYCLES advance in the injected sensor
// read-cycle counter (CommonState.oneWire.readCycleCount) since begin() --
// a cycle counts even with zero/faulty sensors attached (Phase 1), so a
// missing or faulty sensor never blocks confirmation and never forces a
// rollback. Unmet by DEADLINE_MS -> Rollback. Not a Trial boot -> begin()
// leaves the monitor inert; tick() always returns None.
class OtaHealthMonitor {
public:
    static constexpr uint32_t CONFIRM_AFTER_MS = 60000, DEADLINE_MS = 300000;
    static constexpr uint32_t MIN_TICKS = 50, MIN_SENSOR_CYCLES = 10;

    enum class Verdict : uint8_t { None, Confirm, Rollback };

    void begin(bool trial, uint64_t nowMs, uint32_t sensorCycles);

    // Each of Confirm/Rollback is returned exactly once (on the tick that
    // resolves it); every other tick (including all ticks after resolution,
    // and every tick when not pending) returns None.
    Verdict tick(uint64_t nowMs, uint32_t sensorCycles);

    bool pending() const { return _pending; }
    // Seconds until DEADLINE_MS, 0 when not pending or already past it.
    uint16_t remainingS(uint64_t nowMs) const;

private:
    bool _pending = false;
    uint64_t _beginMs = 0;
    uint32_t _baselineCycles = 0;
    uint32_t _ticks = 0;
};
