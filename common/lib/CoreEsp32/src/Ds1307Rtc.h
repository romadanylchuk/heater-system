#pragma once
#include <Wire.h>
#include <stdint.h>
#include "Ds1307Codec.h"

// Thin I2C wrapper around the pure Ds1307Codec (D15): reads/writes the 7 clock
// registers on the DS1307 at BoardConfig's DS1307_ADDR (0x68) and hands the raw
// bytes to decodeDs1307()/encodeDs1307(). The constructor stores a reference to
// the (already-begun) bus and touches no hardware itself.
enum class RtcReadStatus : uint8_t { Ok, Missing, Invalid };

class Ds1307Rtc {
public:
    explicit Ds1307Rtc(TwoWire& wire);

    // Missing on I2C NACK/short read (chip absent); Invalid on any RtcDecode
    // other than Ok (CH bit, bad BCD, bad field, implausible year).
    RtcReadStatus read(uint32_t& utcOut);

    // Encodes utc (CH cleared, 24h mode) and writes all 7 registers. Returns
    // false on an I2C NACK.
    bool write(uint32_t utc);

private:
    TwoWire& _wire;
};
