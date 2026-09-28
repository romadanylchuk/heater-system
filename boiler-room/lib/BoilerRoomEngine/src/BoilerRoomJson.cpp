#include "BoilerRoomJson.h"

namespace {

const char* const PUMP_NAMES[BR_PUMP_COUNT] = {"P1", "P2", "P3"};
constexpr uint8_t ENERGY_DECIMALS = 1;

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

}  // namespace

bool boilerRoomStateJson(const CommonState& s, JsonOut& out, void* ctx) {
    const BoilerRoomStatus* st = static_cast<const BoilerRoomStatus*>(ctx);
    out.beginObject();
    if (st == nullptr || !st->ready) {
        keyBool(out, "ok", false);
        out.endObject();
        return out.ok();
    }
    keyBool(out, "ok", true);

    out.key("pumps");
    out.beginArray();
    for (uint8_t i = 0; i < BR_PUMP_COUNT; ++i) {
        const BoilerRoomPumpStatus& p = st->pump[i];
        out.beginObject();
        keyStr(out, "n", PUMP_NAMES[i]);
        keyBool(out, "on", p.on);
        keyBool(out, "sf", p.safety);
        keyStr(out, "r", pumpReasonKey(p.reason));
        keyBool(out, "as", antiSeizeRunning(s, i));
        out.endObject();
    }
    out.endArray();

    out.key("p3");
    out.beginObject();
    keyStr(out, "mode", p3ModeKey(st->p3Mode));
    keyBool(out, "af", st->antiFreezeRunning);
    keyBool(out, "dump", st->dumpActive);
    keyBool(out, "noOffer", st->offerDisabled);
    keyBool(out, "saved", st->noNeedSaved);
    keyBool(out, "flag", st->noNeedEffective);
    keyBool(out, "link", st->linkUp);
    keyInt(out, "winS", st->offerWindowLeftS);
    keyInt(out, "waitS", st->offerWaitLeftS);
    keyInt(out, "afInS", st->afInS);
    keyInt(out, "idleS", st->p3IdleS);
    out.endObject();

    out.key("energy");
    out.beginObject();
    keyStr(out, "q", energyQualityKey(st->energyQuality));
    out.key("kwh");
    if (st->energyQuality == EnergyQuality::Unavailable) {
        out.null();
    } else {
        out.num(st->energyKWh, ENERGY_DECIMALS);
    }
    out.endObject();

    keyBool(out, "oh", st->overheat);
    keyBool(out, "t6", st->t6Usable);
    out.endObject();
    return out.ok();
}
