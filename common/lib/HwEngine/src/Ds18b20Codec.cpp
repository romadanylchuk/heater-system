#include "Ds18b20Codec.h"

namespace Ds18b20 {

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

bool isValidRom(const uint8_t rom[8]) {
    return rom[0] == FAMILY_CODE && crc8(rom, 7) == rom[7];
}

SensorReading decodeScratchpad(bool presence, const uint8_t scratch[9]) {
    if (!presence) {
        return SensorReading{ReadStatus::NoResponse, 0.0f};
    }

    bool allFF = true;
    bool allZero = true;
    for (size_t i = 0; i < 9; ++i) {
        if (scratch[i] != 0xFF) {
            allFF = false;
        }
        if (scratch[i] != 0x00) {
            allZero = false;
        }
    }
    if (allFF) {
        return SensorReading{ReadStatus::NoResponse, 0.0f};
    }
    if (allZero) {
        return SensorReading{ReadStatus::CrcError, 0.0f};
    }
    if (crc8(scratch, 8) != scratch[8]) {
        return SensorReading{ReadStatus::CrcError, 0.0f};
    }

    int16_t raw = static_cast<int16_t>(static_cast<uint16_t>(scratch[1]) << 8 | static_cast<uint16_t>(scratch[0]));
    float tempC = static_cast<float>(raw) / 16.0f;
    if (tempC < -55.0f || tempC > 125.0f) {
        return SensorReading{ReadStatus::OutOfRange, tempC};
    }
    return SensorReading{ReadStatus::Ok, tempC};
}

}  // namespace Ds18b20
