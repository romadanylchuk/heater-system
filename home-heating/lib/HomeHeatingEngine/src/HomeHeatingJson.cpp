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
    out.endObject();
    return out.ok();
}
