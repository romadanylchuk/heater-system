#pragma once
#include <Wire.h>
#include <stddef.h>
#include <stdint.h>
#include "Command.h"
#include "CommonSettings.h"
#include "CommonState.h"
#include "ConfigEngine.h"
#include "ConfigSchema.h"
#include "CoreRuntime.h"
#include "EventLog.h"
#include "EventTypes.h"
#include "FactoryResetInput.h"
#include "FreeRtosCommandQueue.h"
#include "NvsKvStore.h"
#include "TimeService.h"
#include "Watchdog.h"

// Wiring facade used by both boiler-room/main.cpp and home-heating/main.cpp:
// owns every CoreEsp32 adapter plus the pure CoreEngine services and the
// CoreRuntime that drains commands into them. The constructor touches no
// hardware; begin(wire) does, and MUST be called after the SAFETY relay-off
// statements in setup() -- the DI1 factory-reset countdown (D20) runs inside
// begin() and relies on the relays already being OFF.
constexpr uint32_t CORE_LOOP_PERIOD_MS = 1000;

class CoreServices {
public:
    CoreServices(CommonState& state, const ConfigSchema& schema, const char* fwVersion);

    void begin(TwoWire& wire);
    void tick();  // once per ~1 s loop pass

    CommandQueue& commands() { return _queue; }
    ConfigEngine& config() { return _config; }
    EventLog& events() { return _events; }
    TimeService& time() { return _time; }
    const char* fwVersion() const { return _fwVersion; }

    void setEventPublishHook(EventPublishHook hook, void* ctx) { _events.setPublishHook(hook, ctx); }
    void setResetCountdownHook(ResetCountdownHook hook, void* ctx) {
        _resetHook = hook;
        _resetHookCtx = ctx;
    }
    void setFactoryResetHook(FactoryResetHook hook, void* ctx) { _runtime.setFactoryResetHook(hook, ctx); }

private:
    static void onSettingChanged(size_t index, void* ctx);
    void applyTimezone(const char* tz, EventReason reason);

    CommonState& _state;
    const ConfigSchema& _schema;
    const char* _fwVersion;

    // Declared in this exact order: _events needs _time (as its Clock), and
    // _config/_runtime need _events; member init order follows declaration
    // order regardless of the constructor's initializer-list order.
    NvsKvStore _cfgStore;
    NvsKvStore _logStore;
    TimeService _time;
    EventLog _events;
    ConfigEngine _config;
    FreeRtosCommandQueue _queue;
    CoreRuntime _runtime;

    ResetCountdownHook _resetHook = nullptr;
    void* _resetHookCtx = nullptr;
};
