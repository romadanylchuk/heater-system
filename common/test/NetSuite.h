#pragma once
#include "WifiSuite.h"
#include "MqttSuite.h"
#include "HaSuite.h"
#include "PublishSuite.h"
#include "OtaSuite.h"
#include "ConnectivitySuite.h"

// Aggregates every NetEngine-related native suite (stage 04). Called once
// from CommonSuite.h::runCommonSuite() so both projects run these tests.
// Grows one runXxxSuite() call per phase (D24).
inline void runNetSuite() {
    runWifiSuite();
    runMqttSuite();
    runHaSuite();
    runPublishSuite();
    runOtaSuite();
    runConnectivitySuite();
}
