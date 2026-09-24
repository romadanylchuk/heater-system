#pragma once
#include <stddef.h>
#include <stdint.h>
#include <BoardConfig.h>
#include "EventTypes.h"

// The common part of AppState: fields every controller shares (network, time,
// alarms, diagnostics, 1-Wire scan, relays, system/boot status) plus a read-only
// view of the event log. Written only by the main loop's single writer
// (CoreRuntime); every other task/view only reads it. Value-initialise on
// construction: `CommonState state{};` (D21).
constexpr size_t ONE_WIRE_MAX_DEVICES = 12;

struct NetworkStatus {
    bool wifiConnected;
    int8_t wifiRssi;
    bool mqttConnected;
    bool setupApActive;
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
};

struct RelayArray {
    bool on[RELAY_CHANNEL_COUNT];
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

class EventLog;

struct CommonState {
    NetworkStatus network;
    TimeStatus time;
    AlarmStatus alarms;
    DiagnosticsStatus diag;
    OneWireScan oneWire;
    RelayArray relays;
    SystemStatus system;
    const EventLog* eventLog;  // read-only view of the service's RAM ring, no copy (D21)
};
