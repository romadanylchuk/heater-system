#pragma once
#include <stddef.h>
#include <CommonState.h>
#include "WifiScanList.h"

// JSON snapshot builders for the stage-04 admin web endpoints
// (/api/wifi/status, /api/wifi/scan, /api/version -- D18/D19). ArduinoJson
// lives in the .cpp only, same pattern as HaDiscovery.cpp. None of these
// payloads ever include a password/secret field.

// "off","station","setup_ap","setup_ap_joining".
const char* netWifiModeName(NetWifiMode m);

// {"mode","connected","ssid","ip","rssi","hostname","apActive","apSsid",
// "apIp","mqttEnabled","mqttConnected","scanRunning"}. 0 on overflow.
size_t buildWifiStatusJson(const CommonState& s, char* out, size_t cap);

// {"running":b,"networks":[{"ssid","rssi","secure"}...]}. Trailing (weakest)
// entries that would not fit in `cap` are dropped, so the result is always
// valid JSON with an accurate "running" flag; 0 only if even the empty list
// does not fit.
size_t buildScanJson(const WifiScanList& list, bool running, char* out, size_t cap);

// {"project","fw","web","mismatch","ota":{"inProgress","pendingVerify",
// "bootOutcome"}}. 0 on overflow.
size_t buildVersionJson(const CommonState& s, const char* project, char* out, size_t cap);
