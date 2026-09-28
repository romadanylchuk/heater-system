#pragma once
#include <stddef.h>
#include <HaEntityRegistry.h>
#include "BoilerRoomStatus.h"

// Boiler-room Home Assistant custom entities (stage 07, C11): 15 entries
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
// The state functions read a file-static pointer set once at startup (loop
// task / setup only); nullptr = the status entities are unavailable.
void bindBoilerRoomHaStatus(const BoilerRoomStatus* status);

constexpr size_t BOILER_ROOM_HA_ENTITY_TOTAL = 15;
extern const HaCustomEntity BOILER_ROOM_HA_ENTITIES[BOILER_ROOM_HA_ENTITY_TOTAL];
extern const size_t BOILER_ROOM_HA_ENTITY_COUNT;   // 15
