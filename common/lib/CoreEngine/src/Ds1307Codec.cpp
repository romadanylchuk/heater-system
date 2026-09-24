#include "Ds1307Codec.h"

#include "CivilTime.h"

namespace {

bool bcdToBin(uint8_t bcd, uint8_t& out) {
    uint8_t hi = (bcd >> 4) & 0x0F;
    uint8_t lo = bcd & 0x0F;
    if (hi > 9 || lo > 9) {
        return false;
    }
    out = static_cast<uint8_t>(hi * 10 + lo);
    return true;
}

uint8_t binToBcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

}  // namespace

RtcDecode decodeDs1307(const uint8_t regs[DS1307_REG_COUNT], uint32_t& utcOut) {
    if (regs[0] & 0x80) {  // reg0 bit7 = CH (clock halted)
        return RtcDecode::ClockHalted;
    }

    uint8_t seconds = 0, minutes = 0, hour = 0, day = 0, month = 0, yearOffset = 0;
    if (!bcdToBin(regs[0] & 0x7F, seconds)) return RtcDecode::BadBcd;
    if (!bcdToBin(regs[1] & 0x7F, minutes)) return RtcDecode::BadBcd;

    bool is12h = (regs[2] & 0x40) != 0;  // bit6 = 12h mode select
    bool pm = (regs[2] & 0x20) != 0;     // bit5 = PM (only meaningful in 12h mode)
    uint8_t bcdHour = regs[2] & (is12h ? 0x1F : 0x3F);
    if (!bcdToBin(bcdHour, hour)) return RtcDecode::BadBcd;

    if (!bcdToBin(regs[4] & 0x3F, day)) return RtcDecode::BadBcd;
    if (!bcdToBin(regs[5] & 0x1F, month)) return RtcDecode::BadBcd;
    if (!bcdToBin(regs[6], yearOffset)) return RtcDecode::BadBcd;

    if (is12h) {
        if (hour < 1 || hour > 12) {
            return RtcDecode::BadField;
        }
        hour = (hour == 12) ? 0 : hour;
        if (pm) {
            hour = static_cast<uint8_t>(hour + 12);
        }
    }

    CivilDateTime c{};
    c.year = static_cast<uint16_t>(2000 + yearOffset);
    c.month = month;
    c.day = day;
    c.hour = hour;
    c.minute = minutes;
    c.second = seconds;
    c.weekday = 1;  // not decoded from reg3; isValidCivil() does not require it
    if (!isValidCivil(c)) {
        return RtcDecode::BadField;
    }

    if (c.year < RTC_MIN_VALID_YEAR || c.year > RTC_MAX_VALID_YEAR) {
        return RtcDecode::ImplausibleYear;
    }

    utcOut = civilToEpoch(c);
    return RtcDecode::Ok;
}

void encodeDs1307(uint32_t utc, uint8_t regs[DS1307_REG_COUNT]) {
    CivilDateTime c = epochToCivil(utc);
    regs[0] = binToBcd(c.second);   // CH = 0 (bit7 clear): clock running
    regs[1] = binToBcd(c.minute);
    regs[2] = binToBcd(c.hour);     // 24h mode: bit6 = 0
    regs[3] = c.weekday;            // 1..7, single BCD digit == binary value
    regs[4] = binToBcd(c.day);
    regs[5] = binToBcd(c.month);
    regs[6] = binToBcd(static_cast<uint8_t>(c.year - 2000));
}
