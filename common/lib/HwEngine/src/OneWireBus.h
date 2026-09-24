#pragma once
#include <stddef.h>
#include <stdint.h>

// Pure hardware-facing 1-Wire bus interface (D2). HwEsp32's DallasOneWireBus
// (stage 03 phase 6) implements it over OneWire/DallasTemperature;
// common/test/fakes/FakeOneWireBus implements it for native tests. No
// blocking calls: requestConversionAll() starts the DS18B20 conversion and
// returns immediately -- the caller times the CONVERSION_MS wait itself
// (SensorService::tick, D17).
constexpr size_t ONE_WIRE_SEARCH_MAX = 16;  // raw search capacity (> ONE_WIRE_MAX_DEVICES to detect overflow)

class OneWireBus {
public:
    virtual ~OneWireBus() = default;

    // Fills up to max ROMs (8 bytes each) found on the bus. Sets truncated =
    // true if more devices were present than max. Returns the number written.
    virtual size_t search(uint8_t out[][8], size_t max, bool& truncated) = 0;

    // Starts a conversion on every device (Skip ROM + 0x44 Convert T). Never
    // waits for completion. Returns false if there was no presence pulse.
    virtual bool requestConversionAll() = 0;

    // Reads the 9-byte scratchpad of one device (Match ROM + 0xBE Read
    // Scratchpad). Returns false if there was no presence pulse (device/bus
    // absent).
    virtual bool readScratchpad(const uint8_t address[8], uint8_t scratch[9]) = 0;
};
