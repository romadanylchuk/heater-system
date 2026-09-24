#pragma once
#include <stddef.h>
#include <stdint.h>

// Pure BCD decode/encode for the DS1307 RTC's 7 clock/calendar registers
// (0x00..0x06: seconds, minutes, hours, day-of-week, date, month, year). No I2C
// access here; the adapter (Ds1307Rtc, CoreEsp32) reads/writes the chip and
// hands the raw register bytes to this codec.
constexpr size_t DS1307_REG_COUNT = 7;
constexpr uint16_t RTC_MIN_VALID_YEAR = 2024;
constexpr uint16_t RTC_MAX_VALID_YEAR = 2099;

enum class RtcDecode : uint8_t { Ok, ClockHalted, BadBcd, BadField, ImplausibleYear };

// Decodes regs[0..6] to a UTC epoch (utcOut), handling the seconds CH bit and
// the hours register's 12h/24h mode bit (bit6, PM = bit5 when set).
RtcDecode decodeDs1307(const uint8_t regs[DS1307_REG_COUNT], uint32_t& utcOut);

// Encodes a UTC epoch to regs[0..6]: CH = 0 (clock running), 24h mode.
void encodeDs1307(uint32_t utc, uint8_t regs[DS1307_REG_COUNT]);
