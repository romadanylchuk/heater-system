#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <Descriptors.h>
#include <HwConfig.h>

// Pure text formatting for the OLED pages (stage 06, D15/D17): Wi-Fi/MQTT
// state words, RSSI, temperatures, logical-sensor state and alarm labels.
// English only; output is plain ASCII that displayFitText() can fit.

const char* wifiStateText(const NetworkStatus& n);  // "connected" | "setup AP" | "connecting" | "disconnected"
const char* mqttStateText(const NetworkStatus& n);  // "connected" | "disconnected" | "disabled"
bool formatRssi(const NetworkStatus& n, char* out, size_t cap);  // "-67 dBm"; false + "" if !wifiConnected
void formatTempC(float c, bool valid, char* out, size_t cap);    // "45.2"/"-3.5"; "--" if !valid, !finite, <-55 or >125
const char* sensorStateText(const LogicalSensorStatus& s);       // "--" unassigned, "MISS", "FAULT", "OK", "WAIT"(Unknown)

struct DisplayLabels {
    const AlarmDescriptor* alarms;   // controller table indexed by bit (07/08); may be null/0
    size_t alarmCount;
    const LogicalSensorDesc* sensors;  // HwProjectConfig sensors (names)
    size_t sensorCount;
};

// bit >= 24 && (bit-24) < sensorCount -> "Sensor <name> missing"; bit < alarmCount &&
// alarms[bit].labelEn non-null -> labelEn; else "Alarm <bit>". Always NUL-terminated.
void alarmLabel(uint8_t bit, const DisplayLabels& labels, char* out, size_t cap);

uint8_t alarmCount(uint32_t mask);  // popcount
