#pragma once
#include <stddef.h>
#include <HaEntityRegistry.h>
#include "HomeHeatingStatus.h"

// Home-heating Home Assistant custom entities (stage 08, C13): 16 entries
// appended by HaEntityRegistry::build after the common ones. Keys are binding
// (unique_id = <uniquePrefix>_<key>):
//  - no_need (BinarySensor), p4_reason, k2_mode ("tank"/"bypass", the
//    controller request), k2_reason, k1_position (Sensor, "%", measurement,
//    whole percent; also unavailable while the estimate is unknown), k1_mode,
//    failsafe (Sensor, diagnostic): from the bound HomeHeatingStatus;
//    unavailable ("None") while unbound or not ready.
//  - alarm_h3_failsafe, alarm_h2_failsafe, alarm_h1_failsafe,
//    alarm_multi_failsafe, alarm_h4_failsafe (problem, bits 0..4) and
//    alarm_h1_fault..alarm_h4_fault (problem, diagnostic, bits 8..11): from
//    CommonState.alarms.activeMask.
// H1..H4 temperatures, relay_p4/relay_k2, the heating_enabled switch and the
// h2_set number come from the common registry (settings, sensors, relays).
// The state functions read a file-static pointer set once at startup (loop
// task / setup only); nullptr = the status entities are unavailable.
void bindHomeHeatingHaStatus(const HomeHeatingStatus* status);

constexpr size_t HOME_HEATING_HA_ENTITY_TOTAL = 16;
extern const HaCustomEntity HOME_HEATING_HA_ENTITIES[HOME_HEATING_HA_ENTITY_TOTAL];
extern const size_t HOME_HEATING_HA_ENTITY_COUNT;   // 16
