#pragma once
#include <stdint.h>
#include "BoilerRoomTypes.h"

// Accumulator stored energy (stage 07, C5). E = V * 1.163 * (avg - tBase) / 1000
// kWh over the layers whose state is Ok, clamped at >= 0. Exact with all three
// layers, Estimated with one or two, Unavailable (kWh 0) with none. Pending
// (Unknown) layers count as not working (D23).
enum class EnergyQuality : uint8_t { Exact, Estimated, Unavailable };
struct EnergyResult { EnergyQuality quality; float kWh; };   // kWh = 0 when Unavailable

// t3, t4, t5 accumulator layers; only state == Ok contributes.
EnergyResult computeStoredEnergy(const SensorInput& t3, const SensorInput& t4, const SensorInput& t5,
    float volumeL, float tBase);
const char* energyQualityKey(EnergyQuality q);   // "exact" | "est" | "na"
