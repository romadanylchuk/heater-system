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
//    "fs":"none","nn":b,
//    "tu":{"err":-0.4|null,"lpd":1|-1|0,"lps":4.5,"pt":12,"py":7|null},
//    "st":{"run":b,"blk":"p4_off","el":0,"ps":10,"dt":20.0|null,
//          "res":null|{"o":"result|no_response|aborted","ab":"cancel"|null,
//                      "dt":20.0|null,"r":0.250|null,"p":30|null,"g":2.0|null}}}
// p4.on / k2.byp are the controller's request (the actual relay state stays in
// the base "relays"); "as" is CommonState.antiSeize.output[HH_AS_*].running.
// k1.pos is null while the estimate is unknown, k1.ff while no FF target is
// valid; both are whole percent.
// Stage 09 (C15, D22): tu.err (1 decimal) is null unless h2ErrValid; lps is 1
// decimal; py is null until the first local-day rollover. st.blk / res.o /
// res.ab are the stepBlockKey / stepOutcomeKey / stepAbortKey keys. A dead time
// is "seen" when > 0 (null otherwise); st.dt is the live one of the running
// test. res is null while no test has finished; ab is null unless aborted; r
// (3 decimals) only for a Result; p (integer) and g (1 decimal) only for a
// Result with a valid suggestion. Pure; returns out.ok().
bool homeHeatingStateJson(const CommonState& s, JsonOut& out, void* ctx);
