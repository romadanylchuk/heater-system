#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <cmath>
#include "../../lib/HwEngine/src/Ds18b20Codec.h"
#include "../../lib/HwEngine/src/OneWireBus.h"

// Header-only scriptable OneWireBus fake for native SensorService/Ds18b20
// tests (stage 03 phase 3). Not shipped in firmware (test-only,
// common/test/fakes/).
class FakeOneWireBus : public OneWireBus {
public:
    bool busShorted = false;
    size_t requestCount = 0;
    size_t readCount = 0;
    size_t searchCount = 0;

    // Builds a valid family-0x28 ROM: {0x28, 4 serial bytes from n, 2 zero
    // bytes, crc}.
    static void makeRom(uint32_t serial, uint8_t out[8]) {
        out[0] = Ds18b20::FAMILY_CODE;
        out[1] = static_cast<uint8_t>(serial);
        out[2] = static_cast<uint8_t>(serial >> 8);
        out[3] = static_cast<uint8_t>(serial >> 16);
        out[4] = static_cast<uint8_t>(serial >> 24);
        out[5] = 0x00;
        out[6] = 0x00;
        out[7] = Ds18b20::crc8(out, 7);
    }

    void addDevice(const uint8_t rom[8]) {
        Device d;
        memcpy(d.rom, rom, 8);
        setTempFor(d, 20.0f);
        _devices.push_back(d);
    }

    void setTemp(const uint8_t rom[8], float tempC) {
        Device* d = find(rom);
        if (d != nullptr) {
            setTempFor(*d, tempC);
        }
    }

    void setPowerOn(const uint8_t rom[8]) {
        Device* d = find(rom);
        if (d != nullptr) {
            setTempFor(*d, 85.0f);
        }
    }

    void setCrcError(const uint8_t rom[8]) {
        Device* d = find(rom);
        if (d == nullptr) {
            return;
        }
        d->present = true;
        d->scratch[0] = 0x50;
        d->scratch[1] = 0x05;
        for (size_t i = 2; i < 8; ++i) {
            d->scratch[i] = 0;
        }
        d->scratch[8] = 0x00;  // deliberately wrong CRC
    }

    void setAbsent(const uint8_t rom[8]) {
        Device* d = find(rom);
        if (d != nullptr) {
            d->present = false;
        }
    }

    size_t search(uint8_t out[][8], size_t max, bool& truncated) override {
        ++searchCount;
        size_t n = 0;
        truncated = false;
        for (const Device& d : _devices) {
            if (!d.present) {
                continue;
            }
            if (n >= max) {
                truncated = true;
                continue;
            }
            memcpy(out[n], d.rom, 8);
            ++n;
        }
        return n;
    }

    bool requestConversionAll() override {
        ++requestCount;
        return !busShorted && !_devices.empty();
    }

    bool readScratchpad(const uint8_t address[8], uint8_t scratch[9]) override {
        ++readCount;
        if (busShorted) {
            return false;
        }
        Device* d = find(address);
        if (d == nullptr || !d->present) {
            return false;
        }
        memcpy(scratch, d->scratch, 9);
        return true;
    }

private:
    struct Device {
        uint8_t rom[8] = {};
        bool present = true;
        uint8_t scratch[9] = {};
    };

    static void setTempFor(Device& d, float tempC) {
        d.present = true;
        long raw = lroundf(tempC * 16.0f);
        d.scratch[0] = static_cast<uint8_t>(raw & 0xFF);
        d.scratch[1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
        for (size_t i = 2; i < 8; ++i) {
            d.scratch[i] = 0;
        }
        d.scratch[8] = Ds18b20::crc8(d.scratch, 8);
    }

    Device* find(const uint8_t rom[8]) {
        for (Device& d : _devices) {
            if (memcmp(d.rom, rom, 8) == 0) {
                return &d;
            }
        }
        return nullptr;
    }

    std::vector<Device> _devices;
};
