#include "HardwareServices.h"
#include <time.h>

HardwareServices::HardwareServices(CommonState& state, CoreServices& core, const HwProjectConfig& cfg)
    : _state(state)
    , _core(core)
    , _cfg(cfg)
    , _port()
    , _bus()
    , _runtime(state, core.config(), core.events(), _port, _bus) {}

void HardwareServices::begin(TwoWire& wire) {
    _port.begin(wire);
    _bus.begin();

    const HwStatus st = _runtime.begin(_cfg, _core.time().monoMs());
    if (st != HwStatus::Ok) {
        Serial.printf("[hw] begin failed: %u\n", static_cast<unsigned>(st));
    }

    _core.setCommandExtension(&HwRuntime::commandHook, &_runtime);

    size_t assigned = 0;
    for (uint8_t i = 0; i < _state.sensors.count; ++i) {
        if (_state.sensors.sensor[i].assigned) {
            ++assigned;
        }
    }
    Serial.printf("[hw] status=%u sensors=%u/%u assigned devicesFound=%u k1=%s\n",
                  static_cast<unsigned>(st), static_cast<unsigned>(assigned),
                  static_cast<unsigned>(_state.sensors.count), static_cast<unsigned>(_state.oneWire.count),
                  _state.k1.present ? "yes" : "no");
}

void HardwareServices::tick() {
    LocalTimeInfo local = NO_LOCAL_TIME;
    if (_state.time.valid) {
        const time_t t = time(nullptr);
        struct tm lt {};
        localtime_r(&t, &lt);
        CivilDateTime civil{};
        civil.year = static_cast<uint16_t>(lt.tm_year + 1900);
        civil.month = static_cast<uint8_t>(lt.tm_mon + 1);
        civil.day = static_cast<uint8_t>(lt.tm_mday);
        civil.hour = static_cast<uint8_t>(lt.tm_hour);
        civil.minute = static_cast<uint8_t>(lt.tm_min);
        civil.second = static_cast<uint8_t>(lt.tm_sec);
        civil.weekday = 0;
        local = makeLocalTimeInfo(civil);
    }
    _runtime.tick(_core.time().monoMs(), local);
}

void HardwareServices::fastTick() { _runtime.fastTick(_core.time().monoMs()); }
