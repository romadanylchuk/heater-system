#pragma once
#include <OneWire.h>
#include <DallasTemperature.h>
#include <stddef.h>
#include <stdint.h>
#include <BoardConfig.h>
#include "OneWireBus.h"

// OneWireBus over OneWire + DallasTemperature (D2/D17). requestConversionAll()
// does NOT go through DallasTemperature::requestTemperatures(): the installed
// DallasTemperature 4.0.x hardcodes request_t.result = true regardless of the
// bus reset's presence pulse (see this phase's result "Deviations" section),
// so it can never report "no presence pulse" for a shorted/empty bus. Instead
// this issues Skip ROM + Convert T directly over OneWire using OneWire's own
// presence-pulse-returning reset(), honouring the parasite-power mode that
// DallasTemperature::begin() auto-detected during its own device scan.
class DallasOneWireBus : public OneWireBus {
public:
    explicit DallasOneWireBus(uint8_t pin = ONE_WIRE_PIN);

    // dallas.begin() (also scans/inits devices and detects parasite power),
    // setWaitForConversion(false) (never block), setResolution(12). Resolution
    // is only written to the device EEPROM when it differs from the device's
    // current setting (DallasTemperature library behaviour), so repeated
    // begin() calls do not wear the EEPROM.
    void begin();

    size_t search(uint8_t out[][8], size_t max, bool& truncated) override;
    bool requestConversionAll() override;
    bool readScratchpad(const uint8_t address[8], uint8_t scratch[9]) override;

private:
    OneWire _ow;
    DallasTemperature _dallas;
    bool _parasite = false;
};
