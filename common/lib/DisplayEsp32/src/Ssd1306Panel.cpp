#include "Ssd1306Panel.h"
#include <BoardConfig.h>

namespace {

TwoWire* s_wire = &Wire;       // the shared bus (single panel instance)
volatile bool s_error = false;  // NACK latch, cleared by takeError()
bool s_skipInitDelays = false;

}  // namespace

Ssd1306Panel::Ssd1306Panel() : U8G2() {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, &Ssd1306Panel::byteCb, &Ssd1306Panel::gpioCb);
    // U8g2 stores the 8-bit (shifted) address; START shifts it back (>> 1),
    // exactly like the stock u8x8_byte_arduino_hw_i2c.
    setI2CAddress(static_cast<uint8_t>(SSD1306_ADDR << 1));
}

bool Ssd1306Panel::probe(TwoWire& wire) {
    wire.beginTransmission(SSD1306_ADDR);
    return wire.endTransmission() == 0;
}

bool Ssd1306Panel::takeError() {
    const bool err = s_error;
    s_error = false;
    return err;
}

void Ssd1306Panel::setSkipInitDelays(bool skip) {
    s_skipInitDelays = skip;
}

uint8_t Ssd1306Panel::byteCb(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) {
    switch (msg) {
        case U8X8_MSG_BYTE_INIT:    // never Wire.begin(): bus set up by setup()
        case U8X8_MSG_BYTE_SET_DC:  // I2C has no D/C line
            return 1;
        case U8X8_MSG_BYTE_START_TRANSFER:
            // Once a transfer has failed, the rest of this U8g2 operation is
            // skipped (success-shaped, no Wire calls) until takeError() clears
            // the latch: a stuck bus costs at most one Wire timeout per failed
            // row / re-init instead of one per remaining transaction.
            if (s_error) {
                return 1;
            }
            // No clock change here (the stock driver switches the bus to 400 kHz).
            s_wire->beginTransmission(static_cast<uint8_t>(u8x8_GetI2CAddress(u8x8) >> 1));
            return 1;
        case U8X8_MSG_BYTE_SEND:
            if (s_error) {
                return 1;
            }
            s_wire->write(static_cast<const uint8_t*>(argPtr), argInt);
            return 1;
        case U8X8_MSG_BYTE_END_TRANSFER:
            if (s_error) {
                return 1;
            }
            if (s_wire->endTransmission() != 0) {
                s_error = true;
            }
            return 1;
        default:
            return 0;
    }
}

uint8_t Ssd1306Panel::gpioCb(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) {
    if (s_skipInitDelays && msg == U8X8_MSG_DELAY_MILLI) {
        return 1;
    }
    return u8x8_gpio_and_delay_arduino(u8x8, msg, argInt, argPtr);
}
