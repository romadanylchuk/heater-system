#pragma once
#include <CommonState.h>
#include <JsonOut.h>
#include <WebJson.h>
#include "BoilerRoomStatus.h"

// The "ctl" member of /api/state (stage 07, C11/D22): a StateJsonExtension
// (WebJson.h). ctx = const BoilerRoomStatus*. Writes {"ok":false} when ctx is
// null or the runtime is not ready, else the SPA contract shape:
//   {"ok":true,"pumps":[{"n","on","sf","r","as"} x3],
//    "p3":{"mode","af","dump","noOffer","saved","flag","link","winS","waitS","afInS","idleS"},
//    "energy":{"q","kwh"},"oh","t6"}
// pumps[].on/sf/r are the controller's request (the actual relay state stays
// in the base "relays"); "as" is CommonState.antiSeize.output[i].running.
// energy.kwh is null when q == "na". Pure; returns out.ok().
bool boilerRoomStateJson(const CommonState& s, JsonOut& out, void* ctx);
