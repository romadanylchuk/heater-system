#pragma once
#include <stdint.h>

// Pure relay output port (D2): one byte written to the relay expander.
// HwEsp32's Pcf8574RelayPort (stage 03 phase 6) implements it over
// TwoWire/PCF8574; common/test/fakes/FakeRelayPort implements it for native
// tests. HwRuntime owns the write policy (D13: write on change, re-assert
// every RELAY_REASSERT_MS, retry on failure) -- this interface is just the
// single byte transfer.
class RelayPort {
public:
    virtual ~RelayPort() = default;

    // Writes one byte to the relay output. Returns false on an I2C/bus
    // failure (no presence ack); HwRuntime tracks ioError/ioErrorCount and
    // retries on the next fast tick.
    virtual bool write(uint8_t value) = 0;
};
