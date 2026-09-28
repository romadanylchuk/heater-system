#pragma once
#include <stdint.h>
#include <SensorHistory.h>
#include "HomeHeatingDiagSettings.h"
#include "HomeHeatingTypes.h"
#include "P4FlowCheck.h"

// home-heating diagnostics (stage 09, C11): the H1..H4 history plus the H1
// P4 no-flow check, folded into a warning mask. Pure, time injected.
// Warnings only: the mask never feeds a controller input (D2).
constexpr uint8_t HH_WARN_H1 = 0;
constexpr uint32_t HH_WARN_OWNED_MASK = 0x000000FFu;  // diag.warningMask bits 0..7 (D6)

struct HomeHeatingDiagInputs {
    SensorInput sensor[HH_SENSOR_COUNT];
    bool p4Actual;    // P4 relay ACTUAL state (A1)
    bool k1Known;     // K1 position estimate known
    float k1PosPct;   // estimator value, not the live projection
};

class HomeHeatingDiagnostics {
public:
    void reset();  // history.reset(HH_SENSOR_COUNT), check reset

    // 1. history.tick(now, tempC[], ok[]) with ok = (state == SensorState::Ok) (D3);
    // 2. H1: P4 actual, H1/H2/H3, K1 position;
    // 3. returns the mask (bit HH_WARN_H1 set while the check is active).
    uint32_t update(const HomeHeatingDiagInputs& in, const HomeHeatingDiagSettings& s, uint64_t nowMs);

    const SensorHistory& history() const { return _hist; }
    const P4FlowCheck& h1Check() const { return _h1; }

private:
    SensorHistory _hist;
    P4FlowCheck _h1;
};
