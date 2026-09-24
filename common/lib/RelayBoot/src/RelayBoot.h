#pragma once
#include <Wire.h>

namespace RelayBoot {
// Writes RELAY_ALL_OFF_BYTE to the PCF8574 at PCF8574_RELAY_ADDR on an already-begun bus.
// One retry on NACK/bus error. No delay, no Serial, no exceptions.
// Returns true if a write was ACKed (endTransmission() == 0), false after 2 failed attempts.
bool forceAllOff(TwoWire& wire);
}
