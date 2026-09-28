#include "HomeHeatingJson.h"
#include <math.h>

namespace {

constexpr uint8_t SETPOINT_DECIMALS = 1;
constexpr float PERCENT_MAX = 100.0f;

void keyBool(JsonOut& j, const char* k, bool v) {
    j.key(k);
    j.boolean(v);
}

void keyStr(JsonOut& j, const char* k, const char* v) {
    j.key(k);
    j.str(v);
}

void keyInt(JsonOut& j, const char* k, int64_t v) {
    j.key(k);
    j.integer(v);
}

bool antiSeizeRunning(const CommonState& s, uint8_t i) {
    return i < s.antiSeize.count && i < MAX_ANTI_SEIZE_OUTPUTS && s.antiSeize.output[i].running;
}

// Whole percent, clamped into [0, 100] (non-finite -> 0).
int64_t wholePercent(float pct) {
    if (!(pct >= 0.0f)) {
        return 0;
    }
    return static_cast<int64_t>(lroundf(pct > PERCENT_MAX ? PERCENT_MAX : pct));
}

void keyPercentOrNull(JsonOut& j, const char* k, bool valid, float pct) {
    j.key(k);
    if (valid) {
        j.integer(wholePercent(pct));
    } else {
        j.null();
    }
}

constexpr uint8_t ONE_DECIMAL = 1;
constexpr uint8_t RESPONSE_DECIMALS = 3;

void keyNumOrNull(JsonOut& j, const char* k, bool valid, float v, uint8_t decimals) {
    j.key(k);
    if (valid) {
        j.num(v, decimals);
    } else {
        j.null();
    }
}

// "tu" (stage 09, C15): H2 error, last K1 pulse, K1 pulse counters.
void writeTuning(JsonOut& out, const HomeHeatingStatus& st) {
    out.key("tu");
    out.beginObject();
    keyNumOrNull(out, "err", st.h2ErrValid, st.h2ErrC, ONE_DECIMAL);
    keyInt(out, "lpd", st.lastPulseDir > 0 ? 1 : (st.lastPulseDir < 0 ? -1 : 0));
    out.key("lps");
    out.num(st.lastPulseS, ONE_DECIMAL);
    keyInt(out, "pt", st.pulsesToday);
    out.key("py");
    if (st.pulsesYesterdayValid) {
        out.integer(st.pulsesYesterday);
    } else {
        out.null();
    }
    out.endObject();
}

// "st".res: null while no test has finished. A dead time is "seen" when > 0.
void writeStepResult(JsonOut& out, const StepTestResult& r) {
    out.key("res");
    if (r.outcome == StepOutcome::None) {
        out.null();
        return;
    }
    const bool result = r.outcome == StepOutcome::Result;
    const bool suggest = result && r.suggest.valid;
    out.beginObject();
    keyStr(out, "o", stepOutcomeKey(r.outcome));
    out.key("ab");
    if (r.outcome == StepOutcome::Aborted) {
        out.str(stepAbortKey(r.abort));
    } else {
        out.null();
    }
    keyNumOrNull(out, "dt", r.deadTimeS > 0.0f, r.deadTimeS, ONE_DECIMAL);
    keyNumOrNull(out, "r", result, r.responseCps, RESPONSE_DECIMALS);
    out.key("p");
    if (suggest) {
        out.integer(r.suggest.periodS);
    } else {
        out.null();
    }
    keyNumOrNull(out, "g", suggest, r.suggest.gain, ONE_DECIMAL);
    out.endObject();
}

// "st" (stage 09, C15): the K1 step-test panel.
void writeStepTest(JsonOut& out, const HomeHeatingStepStatus& step) {
    out.key("st");
    out.beginObject();
    keyBool(out, "run", step.running);
    keyStr(out, "blk", stepBlockKey(step.block));
    keyInt(out, "el", step.elapsedS);
    keyInt(out, "ps", step.pulseS);
    keyNumOrNull(out, "dt", step.deadSeen && step.deadTimeS > 0.0f, step.deadTimeS, ONE_DECIMAL);
    writeStepResult(out, step.last);
    out.endObject();
}

}  // namespace

bool homeHeatingStateJson(const CommonState& s, JsonOut& out, void* ctx) {
    const HomeHeatingStatus* st = static_cast<const HomeHeatingStatus*>(ctx);
    out.beginObject();
    if (st == nullptr || !st->ready) {
        keyBool(out, "ok", false);
        out.endObject();
        return out.ok();
    }
    keyBool(out, "ok", true);
    keyBool(out, "en", st->heatingEnabled);
    out.key("set");
    out.num(st->h2Set, SETPOINT_DECIMALS);

    out.key("p4");
    out.beginObject();
    keyBool(out, "on", st->p4On);
    keyStr(out, "r", p4ReasonKey(st->p4Reason));
    keyInt(out, "dlyS", st->p4OffDelayLeftS);
    keyBool(out, "as", antiSeizeRunning(s, HH_AS_P4));
    out.endObject();

    out.key("k2");
    out.beginObject();
    keyBool(out, "byp", st->k2Bypass);
    keyStr(out, "r", k2ReasonKey(st->k2Reason));
    keyBool(out, "as", antiSeizeRunning(s, HH_AS_K2));
    out.endObject();

    out.key("k1");
    out.beginObject();
    keyPercentOrNull(out, "pos", st->k1Known, st->k1PosPct);
    keyInt(out, "mv", st->k1Moving > 0 ? 1 : (st->k1Moving < 0 ? -1 : 0));
    keyStr(out, "m", k1ModeKey(st->k1Mode));
    keyPercentOrNull(out, "ff", st->k1FfValid, st->k1FfPct);
    keyBool(out, "as", antiSeizeRunning(s, HH_AS_K1));
    out.endObject();

    keyStr(out, "fs", failModeKey(st->fail));
    keyBool(out, "nn", st->noNeed);
    writeTuning(out, *st);
    writeStepTest(out, st->step);
    out.endObject();
    return out.ok();
}
