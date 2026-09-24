#pragma once
#include <stdint.h>

// Declaration-only descriptor types for the same table-driven mechanism as
// SettingDescriptor, reserved for later stages (03 sensors/outputs, 07/08 alarms and
// diagnostics). Not used by CoreEngine itself in this stage.
struct SensorDescriptor {
    const char* key;
    const char* labelEn;
    const char* labelUa;
    const char* unit;
};

struct OutputDescriptor {
    const char* key;
    const char* labelEn;
    const char* labelUa;
    uint8_t relayChannel;
};

struct AlarmDescriptor {
    const char* key;
    const char* labelEn;
    const char* labelUa;
};

struct DiagnosticDescriptor {
    const char* key;
    const char* labelEn;
    const char* labelUa;
};
