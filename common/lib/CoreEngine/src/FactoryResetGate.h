#pragma once
#include <stdint.h>
#include "CommonState.h"

// Pure DI1 factory-reset gate state machine (D20): counts down HOLD_MS only if
// DI1 reads closed at power-on, and aborts the moment the input has been open for
// RELEASE_DEBOUNCE_MS straight (so a brief bounce does not abort). No hardware
// access; the adapter (FactoryResetInput, CoreEsp32) supplies the DI1 reads.
class FactoryResetGate {
public:
    static constexpr uint32_t HOLD_MS = 10000;
    static constexpr uint32_t RELEASE_DEBOUNCE_MS = 50;

    // Countdown if closedAtPowerOn, else Inactive (evaluated only once, at boot).
    void begin(bool closedAtPowerOn, uint64_t monoMs);

    // Advances the state machine with the current DI1 reading. A read failure
    // (NACK) must be passed as closed = false by the caller (open), so a missing
    // input chip can never confirm a reset.
    ResetGatePhase update(bool closed, uint64_t monoMs);

    ResetGatePhase phase() const { return _phase; }

    // ceil((HOLD_MS - held) / 1000); 0 once the gate is no longer counting down.
    uint8_t secondsLeft(uint64_t monoMs) const;

private:
    ResetGatePhase _phase = ResetGatePhase::Inactive;
    uint64_t _start = 0;
    uint64_t _openSince = 0;
    bool _openPending = false;
};
