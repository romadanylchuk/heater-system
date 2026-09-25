#pragma once
#include <U8g2lib.h>
#include <Wire.h>
#include <stdint.h>

// U8g2 full-buffer (_F, 1 KB) SSD1306 128x64 driver over the global Wire
// (stage 06, D2/D4/D6). Set up with u8g2_Setup_ssd1306_i2c_128x64_noname_f
// and a custom byte callback instead of the stock u8x8_byte_arduino_hw_i2c:
//   INIT   -> no-op (NEVER Wire.begin: the bus is owned by setup()'s SAFETY statements)
//   SET_DC -> no-op
//   START  -> Wire.beginTransmission(addr)
//   SEND   -> Wire.write
//   END    -> Wire.endTransmission; any non-zero result latches the error flag
// It NEVER calls Wire.setClock: the whole bus (relay PCF8574, DI1 PCF8574,
// DS1307 are standard-mode parts) stays at the 100 kHz Wire.begin default.
// Single instance only (the callbacks use file-static state).
class Ssd1306Panel : public U8G2 {
public:
    Ssd1306Panel();

    static bool probe(TwoWire& wire);  // address-only write to SSD1306_ADDR, true on ACK
    bool takeError();                  // returns and clears the NACK latch

    // Skip U8g2's reset-line millisecond delays (3 x 100 ms in initDisplay).
    // The board wires no OLED reset pin, so they only matter for a panel that
    // has just been powered; used for the runtime re-init of a panel that
    // re-appeared, so recovery does not stall a loop pass for ~300 ms.
    static void setSkipInitDelays(bool skip);

private:
    static uint8_t byteCb(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr);
    static uint8_t gpioCb(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr);
};
