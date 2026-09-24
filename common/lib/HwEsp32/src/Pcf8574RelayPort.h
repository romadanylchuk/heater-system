#pragma once
#include <Wire.h>
#include <stdint.h>
#include <BoardConfig.h>
#include "RelayPort.h"

// RelayPort over a PCF8574 I/O expander on TwoWire (D2/D13). write() is one
// beginTransmission/write/endTransmission with no retry -- HwRuntime owns the
// write policy (write-on-change, 5 s re-assert, retry every fast tick on
// failure). begin() must be called before the first write() (after
// RelayBoot::forceAllOff has already put the board in the safe all-OFF state
// during setup()'s SAFETY block).
class Pcf8574RelayPort : public RelayPort {
public:
    explicit Pcf8574RelayPort(uint8_t address = PCF8574_RELAY_ADDR);

    void begin(TwoWire& wire);
    bool write(uint8_t value) override;

private:
    uint8_t _address;
    TwoWire* _wire = nullptr;
};
