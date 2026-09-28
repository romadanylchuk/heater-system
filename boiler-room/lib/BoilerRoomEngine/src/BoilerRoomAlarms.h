#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Descriptors.h>
#include "BoilerRoomTypes.h"

// Controller alarm bits (stage 07, C6/D5). Bits 0..23 are owned by the
// controller; bits 24..31 belong to stage 03 (sensor missing) and are never
// touched here.
//  - bit 0: overheat latched.
//  - bit 1: T1 Failed (P1 forced).        bit 2: T1 and T2 Failed (P2 forced).
//  - bit 3: T2 Failed, T1 not Failed.     bit 4: T3 Failed (no offer).
//  - bits 8+i: sensor i Unassigned, or Fault without `missing` (a missing
//    sensor is already reported by stage-03 bit 24+i: never alarmed twice).
// Unknown (Pending) raises nothing.
constexpr uint8_t BR_ALARM_OVERHEAT = 0, BR_ALARM_T1_FAILSAFE = 1, BR_ALARM_T1T2_FAILSAFE = 2,
                  BR_ALARM_T2_FAILSAFE = 3, BR_ALARM_T3_FAILSAFE = 4, BR_ALARM_SENSOR_BASE = 8;  // 8..13 = T1..T6
constexpr uint32_t BR_ALARM_OWNED_MASK = 0x00FFFFFFu;   // controller-owned bits 0..23 (never touch 24..31)
constexpr size_t BOILER_ROOM_ALARM_COUNT = 14;          // table covers bits 0..13

extern const AlarmDescriptor BOILER_ROOM_ALARMS[BOILER_ROOM_ALARM_COUNT];   // indexed BY BIT; 5..7 = {nullptr...}

uint32_t computeAlarmMask(const SensorInput t[BR_SENSOR_COUNT], bool overheat);
