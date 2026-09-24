#pragma once
#include <stdint.h>
#include "BoardConfig.h"

// Byte to write to the relay PCF8574 so every output (all 8 pins, incl. unused P6/P7) is inactive.
// activeLow == true  -> 0xFF ; activeLow == false -> 0x00
constexpr uint8_t relayAllOffByte(bool activeLow) { return activeLow ? 0xFF : 0x00; }

constexpr uint8_t RELAY_ALL_OFF_BYTE = relayAllOffByte(RELAY_ACTIVE_LOW);
