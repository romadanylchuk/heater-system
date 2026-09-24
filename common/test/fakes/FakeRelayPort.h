#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "../../lib/HwEngine/src/RelayPort.h"

// Header-only recording RelayPort fake for native HwRuntime tests (stage 03
// phase 5): records every byte HwRuntime::fastTick() writes (whether the
// write "succeeds" or not, per failWrites) so tests can assert the exact
// output sequence -- including the K1 interlock at the output-byte level.
// Not shipped in firmware (test-only, common/test/fakes/).
class FakeRelayPort : public RelayPort {
public:
    bool failWrites = false;
    std::vector<uint8_t> written;   // every byte passed to write(), success or not
    size_t writeCount = 0;

    bool write(uint8_t value) override {
        ++writeCount;
        written.push_back(value);
        return !failWrites;
    }

    uint8_t last() const { return written.empty() ? 0 : written.back(); }
};
