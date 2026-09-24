#include "RelayBoot.h"
#include <RelayMask.h>

namespace RelayBoot {

bool forceAllOff(TwoWire& wire) {
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        wire.beginTransmission(PCF8574_RELAY_ADDR);
        wire.write(RELAY_ALL_OFF_BYTE);
        if (wire.endTransmission() == 0) {
            return true;
        }
    }
    return false;
}

}
