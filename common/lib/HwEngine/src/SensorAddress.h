#pragma once
#include <stddef.h>
#include <stdint.h>

// 8-byte DS18B20 ROM <-> 16 uppercase hex chars, ROM byte order (D4). Pure
// text codec, no allocation.
constexpr size_t SENSOR_ADDRESS_TEXT_LEN = 16;

void formatAddress(const uint8_t address[8], char out[SENSOR_ADDRESS_TEXT_LEN + 1]);

// "" -> false, out zeroed. Accepts upper/lower-case hex; must be exactly 16
// hex characters, else false (out zeroed on any failure).
bool parseAddress(const char* text, uint8_t out[8]);

bool addressEquals(const uint8_t a[8], const uint8_t b[8]);
bool addressIsZero(const uint8_t a[8]);
