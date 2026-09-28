#pragma once
#include <stddef.h>
#include <HaEntityRegistry.h>
#include "BoilerRoomStatus.h"

// Boiler-room Home Assistant custom entities (stage 07, C11; stage 09, C9): 18 entries
// appended by HaEntityRegistry::build after the common ones. Keys are binding
// (unique_id = <uniquePrefix>_<key>):
//  - energy (Sensor, kWh, energy_storage, measurement), energy_estimated
//    (BinarySensor, diagnostic), p3_mode (Sensor), anti_freeze_run
//    (BinarySensor, running): from the bound BoilerRoomStatus; unavailable
//    ("None") while unbound or not ready; energy/energy_estimated also while
//    the stored energy is n/a.
//  - alarm_overheat (heat, bit 0), alarm_t1_failsafe..alarm_t3_failsafe
//    (problem, bits 1..4), alarm_t1_fault..alarm_t6_fault (problem,
//    diagnostic, bits 8..13): from CommonState.alarms.activeMask.
//  - warn_p3_no_flow (B1, bit 0), warn_p1_not_charging (B3, bit 1),
//    warn_p2_no_effect (B6, bit 2): BinarySensor, problem, always available,
//    from CommonState.diag.warningMask (stage 09, C9/D6).
// The state functions read a file-static pointer set once at startup (loop
// task / setup only); nullptr = the status entities are unavailable.
void bindBoilerRoomHaStatus(const BoilerRoomStatus* status);

constexpr size_t BOILER_ROOM_HA_ENTITY_TOTAL = 18;
extern const HaCustomEntity BOILER_ROOM_HA_ENTITIES[BOILER_ROOM_HA_ENTITY_TOTAL];
extern const size_t BOILER_ROOM_HA_ENTITY_COUNT;   // 18
