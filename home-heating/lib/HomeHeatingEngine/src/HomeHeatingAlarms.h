#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Descriptors.h>
#include "HomeHeatingTypes.h"

// Controller alarm bits (stage 08, C8/D17). Bits 0..23 are owned by the
// controller; bits 24..31 belong to stage 03 (sensor missing) and are never
// touched here.
//  - bits 0/1/2/3: fail mode H3 / H2 / H1 / Multi (the fail-safe table row).
//  - bit 4: H4 Failed (K2 forced to bypass).
//  - bits 8+i: sensor i Unassigned, or Fault without `missing` (a missing
//    sensor is already reported by stage-03 bit 24+i: never alarmed twice).
// Unknown (Pending) raises nothing. Independent of heatingEnabled.
constexpr uint8_t HH_ALARM_H3_FAILSAFE = 0, HH_ALARM_H2_FAILSAFE = 1, HH_ALARM_H1_FAILSAFE = 2,
                  HH_ALARM_MULTI_FAILSAFE = 3, HH_ALARM_H4_FAILSAFE = 4, HH_ALARM_SENSOR_BASE = 8;  // 8..11 = H1..H4
constexpr uint32_t HH_ALARM_OWNED_MASK = 0x00FFFFFFu;   // controller-owned bits 0..23 (never touch 24..31)
constexpr size_t HOME_HEATING_ALARM_COUNT = 12;         // table covers bits 0..11

extern const AlarmDescriptor HOME_HEATING_ALARMS[HOME_HEATING_ALARM_COUNT];   // indexed BY BIT; 5..7 = {nullptr...}

uint32_t computeAlarmMask(const SensorInput h[HH_SENSOR_COUNT], FailMode fail);
