#pragma once
#include <stdint.h>

// One pump-response check (stage 09, C6), shared by B1/B3/B6: "pump actually
// ON >= minOn, hot - cold > delta, and cold rose < minRise since the pump
// started". Pure, time injected. It only produces a warning flag and never
// feeds a controller input (D2).
struct RiseCheckParams {
    bool enabled;
    uint32_t minOnMin;
    float deltaC;
    float minRiseC;
};

struct RiseCheckInputs {
    bool pumpOn;   // relay ACTUAL state (A1), not the request
    bool hotOk;    // hot sensor state == SensorState::Ok (D3)
    float hotC;
    bool coldOk;   // cold sensor state == SensorState::Ok (D3)
    float coldC;
};

class PumpRiseCheck {
public:
    void reset();  // not tracking, inactive

    // Returns active(). Rules, in order:
    //  - !pumpOn -> stop tracking, inactive.
    //  - pumpOn && !(hotOk && coldOk) -> stop tracking (A4 pause: timer and
    //    baseline reset), inactive.
    //  - pumpOn && sensors ok && !tracking -> start: startMs = now,
    //    baseline = coldC (the ON edge or the sensor recovery).
    //  - tracking -> cond = (now - startMs >= minOnMin * 60000) &&
    //    (hotC - coldC > deltaC) && (coldC - baseline < minRiseC).
    //  - active = params.enabled && cond (D4: tracking continues while disabled).
    bool update(const RiseCheckInputs& in, const RiseCheckParams& p, uint64_t nowMs);

    bool active() const { return _active; }
    bool tracking() const { return _tracking; }
    float baselineC() const { return _baseline; }

private:
    bool _tracking = false;
    bool _active = false;
    uint64_t _startMs = 0;
    float _baseline = 0.0f;
};
