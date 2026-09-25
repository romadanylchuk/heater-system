#pragma once
#include <stddef.h>
#include <stdint.h>

// Backup-export hand-off between the loop task (which builds the backup
// JSON) and the AsyncTCP task (which serves it) (D9). Not thread-safe on
// its own -- WebServices guards every call with its portMUX.
enum class ExportState : uint8_t { Idle, Requested, Ready };

class ExportSlot {
public:
    static constexpr uint64_t READY_TTL_MS = 60000;

    // Idle -> Requested. Also returns true (no-op) if already
    // Requested/Ready, so a repeated POST /api/backup/export is harmless.
    bool request();

    bool needsBuild() const { return _state == ExportState::Requested; }

    // Takes ownership of `data` (malloc'd by the caller). data == nullptr
    // means the build failed: the slot goes back to Idle with nothing to
    // serve. Replacing an unclaimed Ready buffer frees the old one here
    // (the only place this class calls free()).
    void publish(char* data, size_t len, uint64_t nowMs);

    // Ready -> Idle, ownership moves to the caller (who must free() it).
    bool take(char*& data, size_t& len);

    // A Ready buffer older than READY_TTL_MS goes back to Idle; the buffer
    // is returned (not freed) for the caller to free outside the lock.
    // Returns nullptr otherwise.
    char* expire(uint64_t nowMs);

    ExportState state() const { return _state; }

private:
    ExportState _state = ExportState::Idle;
    char* _data = nullptr;
    size_t _len = 0;
    uint64_t _readyAtMs = 0;
};
