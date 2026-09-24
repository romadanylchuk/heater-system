#include "DallasOneWireBus.h"
#include <string.h>

DallasOneWireBus::DallasOneWireBus(uint8_t pin) : _ow(pin), _dallas(&_ow) {}

void DallasOneWireBus::begin() {
    _dallas.begin();
    _dallas.setWaitForConversion(false);
    _dallas.setResolution(12);
    _parasite = _dallas.isParasitePowerMode();
}

size_t DallasOneWireBus::search(uint8_t out[][8], size_t max, bool& truncated) {
    truncated = false;
    _ow.reset_search();
    size_t count = 0;
    uint8_t addr[8];
    while (_ow.search(addr)) {
        if (count < max) {
            memcpy(out[count], addr, 8);
            ++count;
        } else {
            truncated = true;
            break;
        }
    }
    return count;
}

bool DallasOneWireBus::requestConversionAll() {
    if (_ow.reset() == 0) {
        return false;
    }
    _ow.skip();
    _ow.write(0x44, _parasite ? 1 : 0);
    return true;
}

bool DallasOneWireBus::readScratchpad(const uint8_t address[8], uint8_t scratch[9]) {
    return _dallas.readScratchPad(address, scratch);
}
