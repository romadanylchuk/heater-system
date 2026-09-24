#include "FactoryResetInput.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <BoardConfig.h>
#include "Watchdog.h"

namespace {

uint64_t monoMsNow() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

}  // namespace

namespace FactoryResetInput {

bool readDi1(TwoWire& wire, bool& closedOut) {
    if (wire.requestFrom(PCF8574_INPUT_ADDR, static_cast<uint8_t>(1)) != 1) {
        return false;
    }
    uint8_t value = static_cast<uint8_t>(wire.read());
    uint8_t bit = (value >> FACTORY_RESET_INPUT) & 0x01;
    closedOut = INPUT_ACTIVE_LOW ? (bit == 0) : (bit == 1);
    return true;
}

ResetGatePhase runBootCountdown(TwoWire& wire, CommonState& state, ResetCountdownHook hook, void* ctx) {
    bool closed = false;
    bool readOk = readDi1(wire, closed);

    FactoryResetGate gate;
    uint64_t startMs = monoMsNow();
    gate.begin(readOk && closed, startMs);

    ResetGatePhase phase = gate.phase();
    uint8_t secondsLeft = gate.secondsLeft(startMs);
    state.system.resetPhase = phase;
    state.system.resetSecondsLeft = secondsLeft;
    if (hook != nullptr) {
        hook(phase, secondsLeft, ctx);
    }

    uint32_t lastLogSecond = 0xFFFFFFFFu;

    while (phase == ResetGatePhase::Countdown) {
        delay(20);
        Watchdog::feed();

        uint64_t nowMs = monoMsNow();
        readOk = readDi1(wire, closed);
        const bool effectiveClosed = readOk && closed;  // a read failure counts as open

        const ResetGatePhase newPhase = gate.update(effectiveClosed, nowMs);
        const uint8_t newSecondsLeft = gate.secondsLeft(nowMs);

        if (newPhase != phase || newSecondsLeft != secondsLeft) {
            phase = newPhase;
            secondsLeft = newSecondsLeft;
            state.system.resetPhase = phase;
            state.system.resetSecondsLeft = secondsLeft;
            if (hook != nullptr) {
                hook(phase, secondsLeft, ctx);
            }
        }

        const uint32_t nowSecond = static_cast<uint32_t>(nowMs / 1000);
        if (nowSecond != lastLogSecond) {
            lastLogSecond = nowSecond;
            Serial.printf("[factory-reset] DI1 held, %u s left\n", static_cast<unsigned int>(secondsLeft));
        }
    }

    return phase;
}

}  // namespace FactoryResetInput
