#pragma once
#include <Wire.h>
#include <stdint.h>
#include "CommonState.h"
#include "FactoryResetGate.h"

// DI1 factory-reset input: PCF8574 @ PCF8574_INPUT_ADDR (BoardConfig.h), bit
// FACTORY_RESET_INPUT, polarity BoardConfig::INPUT_ACTIVE_LOW. Drives the pure
// FactoryResetGate (D20) with a blocking boot-time countdown; must run after the
// SAFETY relay-off statements in setup(), so relays are already OFF throughout.
using ResetCountdownHook = void (*)(ResetGatePhase phase, uint8_t secondsLeft, void* ctx);

namespace FactoryResetInput {

// false on an I2C NACK/short read; closedOut is only meaningful when this
// returns true.
bool readDi1(TwoWire& wire, bool& closedOut);

// Evaluated only if DI1 reads closed at power-on (Inactive otherwise, including
// on a read failure). Blocks, polling every 20 ms and feeding Watchdog each
// pass, updating state.system.resetPhase/resetSecondsLeft and calling hook on
// every phase or seconds-left change. A read failure mid-countdown counts as
// "open" (D20), so a missing input chip can never confirm a reset. Returns the
// terminal phase (Inactive/Aborted/Confirmed).
ResetGatePhase runBootCountdown(TwoWire& wire, CommonState& state, ResetCountdownHook hook, void* ctx);

}  // namespace FactoryResetInput
