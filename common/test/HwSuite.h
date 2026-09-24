#pragma once
#include "AntiSeizeSuite.h"
#include "HwConfigSuite.h"
#include "HwRuntimeSuite.h"
#include "K1DriverSuite.h"
#include "RelayBankSuite.h"
#include "SensorServiceSuite.h"
#include "SensorSuite.h"

// Aggregates every HwEngine-related native suite (stage 03). Called once from
// CommonSuite.h::runCommonSuite() so both projects run these tests. Grows one
// runXxxSuite() call per phase (D24).
inline void runHwSuite() {
    runHwConfigSuite();
    runRelayBankSuite();
    runK1DriverSuite();
    runSensorSuite();
    runSensorServiceSuite();
    runAntiSeizeSuite();
    runHwRuntimeSuite();
}
