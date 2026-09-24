#include "OtaBoot.h"

#include <string.h>

OtaBootOutcome classifyOtaBoot(const OtaBootInfo& info) {
    bool pending = info.imageState == OtaImageState::PendingVerify;
    bool matchesMarker = info.markerPresent && info.runningLabel != nullptr && info.markerTarget != nullptr &&
                          strcmp(info.runningLabel, info.markerTarget) == 0;

    if (matchesMarker) {
        return pending ? OtaBootOutcome::Trial : OtaBootOutcome::UpdatedNoRollback;
    }
    if (info.markerPresent) {
        return OtaBootOutcome::RolledBack;
    }
    if (pending) {
        return OtaBootOutcome::Trial;
    }
    return OtaBootOutcome::Normal;
}

bool OtaMarkerStore::load(char* out, size_t cap) const {
    return _store.getStr(OTA_MARKER_KEY, out, cap) == StoreStatus::Ok;
}

bool OtaMarkerStore::save(const char* label) {
    if (_store.setStr(OTA_MARKER_KEY, label) != StoreStatus::Ok) {
        return false;
    }
    return _store.commit() == StoreStatus::Ok;
}

bool OtaMarkerStore::clear() {
    StoreStatus s = _store.remove(OTA_MARKER_KEY);
    if (s != StoreStatus::Ok && s != StoreStatus::NotFound) {
        return false;
    }
    return _store.commit() == StoreStatus::Ok;
}

void OtaHealthMonitor::begin(bool trial, uint64_t nowMs, uint32_t sensorCycles) {
    _pending = trial;
    _beginMs = nowMs;
    _baselineCycles = sensorCycles;
    _ticks = 0;
}

OtaHealthMonitor::Verdict OtaHealthMonitor::tick(uint64_t nowMs, uint32_t sensorCycles) {
    if (!_pending) {
        return Verdict::None;
    }
    ++_ticks;

    uint32_t cycleDelta = sensorCycles - _baselineCycles;  // read cycles advance regardless of sensor presence
    uint64_t elapsedMs = nowMs - _beginMs;

    if (elapsedMs >= CONFIRM_AFTER_MS && _ticks >= MIN_TICKS && cycleDelta >= MIN_SENSOR_CYCLES) {
        _pending = false;
        return Verdict::Confirm;
    }
    if (elapsedMs >= DEADLINE_MS) {
        _pending = false;
        return Verdict::Rollback;
    }
    return Verdict::None;
}

uint16_t OtaHealthMonitor::remainingS(uint64_t nowMs) const {
    if (!_pending) {
        return 0;
    }
    uint64_t deadlineAt = _beginMs + DEADLINE_MS;
    if (nowMs >= deadlineAt) {
        return 0;
    }
    return static_cast<uint16_t>((deadlineAt - nowMs) / 1000);
}
