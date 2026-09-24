#include "Pcf8574RelayPort.h"

Pcf8574RelayPort::Pcf8574RelayPort(uint8_t address) : _address(address) {}

void Pcf8574RelayPort::begin(TwoWire& wire) { _wire = &wire; }

bool Pcf8574RelayPort::write(uint8_t value) {
    if (_wire == nullptr) {
        return false;
    }
    _wire->beginTransmission(_address);
    _wire->write(value);
    return _wire->endTransmission() == 0;
}
