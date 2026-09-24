#pragma once
#include <stddef.h>
#include <stdint.h>

// DS18B20 CRC8, ROM validation and scratchpad decode. Pure, no hardware
// access, no history (SensorDebounce owns the power-on-sentinel history
// check, D14).
enum class ReadStatus : uint8_t { Ok, NoResponse, CrcError, OutOfRange, PowerOnValue };

struct SensorReading {
    ReadStatus status;
    float tempC;  // meaningful only when status == Ok or PowerOnValue
};

namespace Ds18b20 {

constexpr uint8_t FAMILY_CODE = 0x28;

// Dallas/Maxim CRC8 (poly 0x8C, reflected), table-free loop.
uint8_t crc8(const uint8_t* data, size_t len);

// crc8(rom, 7) == rom[7] && rom[0] == FAMILY_CODE.
bool isValidRom(const uint8_t rom[8]);

// Decodes a 9-byte DS18B20 scratchpad already read off the bus.
// presence == false, or all 9 bytes 0xFF -> NoResponse.
// all 9 bytes 0x00, or scratch[8] != crc8(scratch, 8) -> CrcError.
// raw = int16_t(scratch[1] << 8 | scratch[0]); tempC = raw / 16.0f.
// tempC outside [-55, 125] -> OutOfRange.
// Otherwise -> Ok. Does NOT apply the 85 degC power-on rule (D14).
SensorReading decodeScratchpad(bool presence, const uint8_t scratch[9]);

}  // namespace Ds18b20
