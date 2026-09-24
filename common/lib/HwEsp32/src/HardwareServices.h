#pragma once
#include <Wire.h>
#include <CommonState.h>
#include <CoreServices.h>
#include "DallasOneWireBus.h"
#include "HwConfig.h"
#include "HwRuntime.h"
#include "Pcf8574RelayPort.h"

// Wiring facade used by both boiler-room/main.cpp and home-heating/main.cpp,
// constructed next to CoreServices: owns the real RelayPort/OneWireBus
// adapters and the pure HwRuntime, and drives begin()/tick()/fastTick() from
// the main loop (D23). The constructor touches no hardware; begin(wire)
// does, and MUST be called after core.begin() (HwRuntime::begin() reads
// ConfigEngine settings that core.begin() has just loaded from NVS).
class HardwareServices {
public:
    HardwareServices(CommonState& state, CoreServices& core, const HwProjectConfig& cfg);

    // _port.begin(wire); _bus.begin(); _runtime.begin(cfg, core.time().monoMs());
    // registers HwRuntime::commandHook with CoreServices' CommandExtensionHandler
    // hook so AssignSensor/ClearSensor/RescanOneWire commands reach HwRuntime.
    void begin(TwoWire& wire);

    // ~1 s: builds LocalTimeInfo from time(nullptr)/localtime_r (TZ already
    // applied by TimeService) when state.time.valid, else NO_LOCAL_TIME, then
    // HwRuntime::tick().
    void tick();

    // ~100 ms: HwRuntime::fastTick().
    void fastTick();

    HwRuntime& runtime() { return _runtime; }

private:
    CommonState& _state;
    CoreServices& _core;
    const HwProjectConfig& _cfg;

    Pcf8574RelayPort _port;
    DallasOneWireBus _bus;
    HwRuntime _runtime;
};
