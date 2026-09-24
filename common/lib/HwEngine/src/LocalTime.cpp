#include "LocalTime.h"

LocalTimeInfo makeLocalTimeInfo(const CivilDateTime& local) {
    CivilDateTime midnight = local;
    midnight.hour = 0;
    midnight.minute = 0;
    midnight.second = 0;

    const uint32_t epochMidnight = civilToEpoch(midnight);

    LocalTimeInfo info{};
    info.valid = true;
    info.dayIndex = epochMidnight / 86400u;
    info.minuteOfDay = static_cast<uint16_t>(static_cast<uint16_t>(local.hour) * 60u + local.minute);
    return info;
}
