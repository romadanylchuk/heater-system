#include "StoredEnergy.h"

EnergyResult computeStoredEnergy(const SensorInput& t3, const SensorInput& t4, const SensorInput& t5,
    float volumeL, float tBase) {
    const SensorInput* layers[3] = {&t3, &t4, &t5};
    float sum = 0.0f;
    int n = 0;
    for (const SensorInput* l : layers) {
        if (l->state == SensorState::Ok) {
            sum += l->tempC;
            ++n;
        }
    }
    if (n == 0) {
        return EnergyResult{EnergyQuality::Unavailable, 0.0f};
    }
    const float avg = sum / static_cast<float>(n);
    float kWh = volumeL * 1.163f * (avg - tBase) / 1000.0f;
    if (kWh < 0.0f) {
        kWh = 0.0f;
    }
    return EnergyResult{n == 3 ? EnergyQuality::Exact : EnergyQuality::Estimated, kWh};
}

const char* energyQualityKey(EnergyQuality q) {
    switch (q) {
        case EnergyQuality::Exact: return "exact";
        case EnergyQuality::Estimated: return "est";
        case EnergyQuality::Unavailable:
        default: return "na";
    }
}
