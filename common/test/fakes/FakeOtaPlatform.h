#pragma once
#include <stddef.h>
#include <string.h>
#include <string>
#include "../../lib/NetEngine/src/OtaPlatform.h"

// Header-only OtaPlatform fake for native tests: settable running-image
// state/labels plus call counts for markRunningValid()/rollbackAndReboot()/restart()/
// abortUpdate(), and a settable Update-running flag.
// Not shipped in firmware (test-only, common/test/fakes/).
class FakeOtaPlatform : public OtaPlatform {
public:
    OtaImageState state = OtaImageState::Valid;
    std::string running = "app0";
    std::string booting = "app0";  // bootLabel(): differs from running only right after a successful OTA
    bool markValidResult = true;

    int markRunningValidCount = 0;
    int rollbackAndRebootCount = 0;
    int restartCount = 0;

    OtaImageState runningImageState() override { return state; }

    bool runningLabel(char* out, size_t cap) override { return copyInto(running, out, cap); }

    bool bootLabel(char* out, size_t cap) override { return copyInto(booting, out, cap); }

    bool markRunningValid() override {
        ++markRunningValidCount;
        return markValidResult;
    }

    void rollbackAndReboot() override { ++rollbackAndRebootCount; }

    void restart() override { ++restartCount; }

    // Global Update object: running by default, i.e. a web /ota/start whose
    // Update.begin() succeeded. abortUpdate() closes it.
    bool updateRunningFlag = true;
    int abortUpdateCount = 0;

    bool updateRunning() override { return updateRunningFlag; }

    void abortUpdate() override {
        ++abortUpdateCount;
        updateRunningFlag = false;
    }

private:
    static bool copyInto(const std::string& v, char* out, size_t cap) {
        if (v.size() + 1 > cap) {
            return false;
        }
        memcpy(out, v.data(), v.size());
        out[v.size()] = '\0';
        return true;
    }
};
