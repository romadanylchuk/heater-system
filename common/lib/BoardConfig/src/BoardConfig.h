#pragma once
#include <stdint.h>

// KC868-A6 board map (single source of truth for pins / I2C addresses)
constexpr uint8_t I2C_SDA_PIN          = 4;
constexpr uint8_t I2C_SCL_PIN          = 15;
constexpr uint8_t ONE_WIRE_PIN         = 32;
constexpr uint8_t PCF8574_RELAY_ADDR   = 0x24;
constexpr uint8_t PCF8574_INPUT_ADDR   = 0x22;
constexpr uint8_t SSD1306_ADDR         = 0x3C;
constexpr uint8_t DS1307_ADDR          = 0x68;
constexpr uint8_t RELAY_CHANNEL_COUNT  = 6;     // R1..R6 = PCF8574 P0..P5
constexpr uint8_t FACTORY_RESET_INPUT  = 0;     // DI1 = PCF8574 @0x22 bit 0 (reserved)

// TODO(board-check): KC868-A6 opto inputs pull the PCF8574 pin low when closed;
// confirm with DI1 on the real board using the README bring-up checklist.
constexpr bool INPUT_ACTIVE_LOW = true;

// Relay map (comment table): R1 P1 / K2, R2 P2 / K1 motor power,
// R3 P3 / K1 direction, R4 spare / P4, R5-R6 spare / spare  (boiler-room / home-heating)

// TODO(board-check): KC868-A6 relays are documented as active-LOW (all-OFF = 0xFF).
// Confirm on the real board using the README bring-up checklist; flip to false if wrong.
constexpr bool RELAY_ACTIVE_LOW = true;
