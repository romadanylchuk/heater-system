#pragma once
#include <CommonState.h>
#include <JsonOut.h>
#include <WebJson.h>
#include "HomeHeatingStatus.h"

// The "ctl" member of /api/state (stage 08, C13): a StateJsonExtension
// (WebJson.h). ctx = const HomeHeatingStatus*. Writes {"ok":false} when ctx is
// null or the runtime is not ready, else:
//   {"ok":true,"en":b,"set":40.0,
//    "p4":{"on":b,"r":"demand","dlyS":0,"as":b},
//    "k2":{"byp":b,"r":"charging","as":b},
//    "k1":{"pos":35|null,"mv":1|0|-1,"m":"normal","ff":42|null,"as":b},
//    "fs":"none","nn":b}
// p4.on / k2.byp are the controller's request (the actual relay state stays in
// the base "relays"); "as" is CommonState.antiSeize.output[HH_AS_*].running.
// k1.pos is null while the estimate is unknown, k1.ff while no FF target is
// valid; both are whole percent. Pure; returns out.ok().
bool homeHeatingStateJson(const CommonState& s, JsonOut& out, void* ctx);
