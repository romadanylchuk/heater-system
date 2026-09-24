#pragma once
#include <stddef.h>
#include <stdint.h>
#include <BoardConfig.h>
#include "EventTypes.h"
#include "HardwareStatus.h"

// The common part of AppState: fields every controller shares (network, time,
// alarms, diagnostics, 1-Wire scan, relays, sensors, K1, anti-seize,
// system/boot status) plus a read-only view of the event log. Written only by
// the main loop's single writer (CoreRuntime); every other task/view only
// reads it. Value-initialise on construction: `CommonState state{};` (D21).
// Stage 03 extends RelayArray/OneWireScan and appends sensors/k1/antiSeize
// additively; nothing that read relays/oneWire before stage 03 is broken.
constexpr size_t ONE_WIRE_MAX_DEVICES = 12;

// Stage 04 (connectivity): Wi-Fi/AP text field sizes and version string size.
constexpr size_t NET_IP_TEXT_LEN = 15;     // "255.255.255.255"
constexpr size_t NET_NAME_TEXT_LEN = 32;   // SSID / hostname / AP SSID
constexpr size_t VERSION_TEXT_LEN = 40;

enum class NetWifiMode : uint8_t { Off = 0, Station, SetupAp, SetupApJoining };
enum class OtaSource : uint8_t { None = 0, Web, Espota };
enum class OtaBootOutcome : uint8_t { Normal = 0, Trial, UpdatedNoRollback, RolledBack };

struct NetworkStatus {
    bool wifiConnected;
    int8_t wifiRssi;
    bool mqttConnected;    // effective: transport connected AND Wi-Fi up (stage 04)
    bool setupApActive;
    NetWifiMode wifiMode;
    char ip[NET_IP_TEXT_LEN + 1];
    char ssid[NET_NAME_TEXT_LEN + 1];
    char hostname[NET_NAME_TEXT_LEN + 1];
    char apSsid[NET_NAME_TEXT_LEN + 1];
    char apIp[NET_IP_TEXT_LEN + 1];
    uint32_t wifiDownS;
    uint32_t wifiConnectCount;
    bool scanRunning;
    uint8_t scanCount;
    bool mqttEnabled;
    uint32_t mqttConnectCount;
    uint32_t mqttDroppedCommands;
};

enum class TimeSourceKind : uint8_t { None, Rtc, Ntp };

struct TimeStatus {
    uint32_t utcNow;
    bool valid;
    TimeSourceKind source;
    bool rtcPresent;
    bool rtcValid;
    uint32_t lastNtpSyncUtc;
};

struct AlarmStatus {
    uint32_t activeMask;  // bits defined by controller alarm tables
};

struct DiagnosticsStatus {
    uint32_t warningMask;
    bool nvsError;
    bool rtcMissing;
    bool rtcInvalid;
    bool tzInvalid;
    bool watchdogError;
    bool commandQueueFull;
};

struct OneWireScan {
    uint8_t count;
    uint8_t address[ONE_WIRE_MAX_DEVICES][8];
    bool done;
    uint8_t logical[ONE_WIRE_MAX_DEVICES];   // NO_LOGICAL_SENSOR = new/unassigned
    float tempC[ONE_WIRE_MAX_DEVICES];
    bool tempValid[ONE_WIRE_MAX_DEVICES];
    bool overflow;
    uint32_t scanCount;
    uint32_t readCycleCount;  // stage 04: completed bus-read cycles (OTA rollback health input)
};

struct RelayArray {
    bool on[RELAY_CHANNEL_COUNT];                        // actual state (unchanged meaning)
    RelayChannelStatus channel[RELAY_CHANNEL_COUNT];
    bool ioError;
    uint32_t ioErrorCount;
    bool inhibited;  // stage 04: OTA output inhibit active (D13)
};

enum class ResetGatePhase : uint8_t { Inactive, Countdown, Aborted, Confirmed };

struct SystemStatus {
    ResetCause lastResetCause;
    uint32_t uptimeS;
    bool setupApRequested;
    bool rebootRequested;
    uint64_t rebootAtMs;
    ResetGatePhase resetPhase;
    uint8_t resetSecondsLeft;
    uint32_t lastCommandId;
    uint8_t lastCommandStatus;
};

struct OtaStatus {
    bool inProgress;
    OtaSource source;
    OtaBootOutcome bootOutcome;
    bool pendingVerify;
    uint16_t verifyRemainingS;
};

struct VersionStatus {
    char fw[VERSION_TEXT_LEN + 1];
    char web[VERSION_TEXT_LEN + 1];
    bool webMismatch;
};

class EventLog;

struct CommonState {
    NetworkStatus network;
    TimeStatus time;
    AlarmStatus alarms;
    DiagnosticsStatus diag;
    OneWireScan oneWire;
    RelayArray relays;
    SensorArray sensors;
    K1Status k1;
    AntiSeizeStatus antiSeize;
    OtaStatus ota;
    VersionStatus versions;
    SystemStatus system;
    const EventLog* eventLog;  // read-only view of the service's RAM ring, no copy (D21)
};
