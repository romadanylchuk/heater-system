#pragma once
#include <stdint.h>

// K1 (home-heating mixing valve) power + direction pulse state machine (D11).
// R2 = motor power, R3 = direction (energised/NO = OPEN, de-energised/NC =
// CLOSE). The direction output is NEVER changed while power is ON: this class
// is the sole place that decides powerOn()/directionOpen(), and it enforces
// the interlock by construction (a direction change is only ever attempted
// while !powerOn(), and power only ever turns on once the direction has been
// settled for DEAD_TIME_MS). HwRuntime replays powerOn()/directionOpen() into
// RelayBank every fast tick; RelayBank never reorders them relative to a
// single tick, so the interlock holds at the relay-output-byte level too.
// Pure logic: time is injected via tick(now)/requestPulse(..., now).
enum class K1Direction : uint8_t { Close = 0, Open = 1 };
enum class K1Owner : uint8_t { None = 0, Control, AntiSeize };

struct K1Motion {
    uint32_t openMs;
    uint32_t closeMs;
};

class K1Driver {
public:
    static constexpr uint32_t DEAD_TIME_MS = 1000;
    static constexpr uint32_t MAX_PULSE_MS = 600000;

    // Power OFF, direction Close (de-energised), as if it has been at rest for
    // a full dead time: a CLOSE pulse right after begin() powers ON on its
    // first tick, while an OPEN pulse waits the dead time after lastPowerOff
    // before the direction can change.
    void begin(uint64_t nowMs);

    // Policy "replace" (D11):
    //  - same direction while running: the end time becomes now + duration (no
    //    power cycle);
    //  - opposite direction while running: power OFF now, then the reversal
    //    sequence (dead time, direction change, dead time, power ON);
    //  - not running: replaces any pending request.
    // durationMs 0 or > MAX_PULSE_MS -> false (no state change).
    bool requestPulse(K1Direction dir, uint32_t durationMs, uint64_t nowMs, K1Owner owner = K1Owner::Control);

    // Power OFF immediately, drop any pending request. Motion done so far is
    // accounted for exactly as a normal run end.
    void cancel(uint64_t nowMs);

    void tick(uint64_t nowMs);

    bool powerOn() const { return _powerOn; }        // desired R2
    bool directionOpen() const { return _dirOpen; }   // desired R3 (true = energised = OPEN)
    bool busy() const { return _pending || _running; }
    bool running() const { return _running; }
    K1Direction direction() const;  // of the current/pending request (Close when idle)
    K1Owner owner() const;          // None when idle
    uint32_t currentRunMs(uint64_t nowMs) const;

    // Accumulated power-ON ms per direction since the last call; resets to 0.
    K1Motion takeMotion();

    bool hasMoved() const { return _hasMoved; }
    uint64_t lastMoveMs() const { return _lastMoveMs; }

private:
    void stopRun(uint64_t nowMs);

    bool _powerOn = false;
    bool _dirOpen = false;
    uint64_t _lastPowerOffMs = 0;
    uint64_t _lastDirChangeMs = 0;

    bool _running = false;
    K1Direction _runDir = K1Direction::Close;
    uint64_t _runStartMs = 0;
    uint64_t _runEndMs = 0;
    K1Owner _owner = K1Owner::None;

    bool _pending = false;
    K1Direction _pendDir = K1Direction::Close;
    uint32_t _pendDurMs = 0;
    K1Owner _pendOwner = K1Owner::None;

    K1Motion _motion = {0, 0};
    bool _hasMoved = false;
    uint64_t _lastMoveMs = 0;
};
