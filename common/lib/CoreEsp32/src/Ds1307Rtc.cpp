#include "Ds1307Rtc.h"
#include <BoardConfig.h>

Ds1307Rtc::Ds1307Rtc(TwoWire& wire) : _wire(wire) {}

RtcReadStatus Ds1307Rtc::read(uint32_t& utcOut) {
    _wire.beginTransmission(DS1307_ADDR);
    _wire.write(static_cast<uint8_t>(0));
    if (_wire.endTransmission() != 0) {
        return RtcReadStatus::Missing;
    }

    if (_wire.requestFrom(DS1307_ADDR, static_cast<uint8_t>(DS1307_REG_COUNT)) != DS1307_REG_COUNT) {
        return RtcReadStatus::Missing;
    }

    uint8_t regs[DS1307_REG_COUNT] = {};
    for (size_t i = 0; i < DS1307_REG_COUNT; ++i) {
        regs[i] = static_cast<uint8_t>(_wire.read());
    }

    RtcDecode decoded = decodeDs1307(regs, utcOut);
    return decoded == RtcDecode::Ok ? RtcReadStatus::Ok : RtcReadStatus::Invalid;
}

bool Ds1307Rtc::write(uint32_t utc) {
    uint8_t regs[DS1307_REG_COUNT] = {};
    encodeDs1307(utc, regs);

    _wire.beginTransmission(DS1307_ADDR);
    _wire.write(static_cast<uint8_t>(0));
    for (size_t i = 0; i < DS1307_REG_COUNT; ++i) {
        _wire.write(regs[i]);
    }
    return _wire.endTransmission() == 0;
}
