#include "HwConfig.h"

#include <string.h>
#include <BoardConfig.h>

namespace {

bool channelConfigured(const HwProjectConfig& cfg, uint8_t channel) {
    for (size_t i = 0; i < cfg.relayCount; ++i) {
        if (cfg.relays[i].channel == channel) {
            return true;
        }
    }
    return false;
}

bool validateRelays(const HwProjectConfig& cfg) {
    if (cfg.relayCount > RELAY_CHANNEL_COUNT) {
        return false;
    }
    for (size_t i = 0; i < cfg.relayCount; ++i) {
        const RelayChannelDesc& r = cfg.relays[i];
        if (r.channel >= RELAY_CHANNEL_COUNT || r.name == nullptr) {
            return false;
        }
        for (size_t j = i + 1; j < cfg.relayCount; ++j) {
            if (cfg.relays[j].channel == r.channel) {
                return false;
            }
        }
    }
    return true;
}

bool validateK1(const HwProjectConfig& cfg) {
    bool foundPower = false;
    bool foundDirection = false;
    for (size_t i = 0; i < cfg.relayCount; ++i) {
        const RelayChannelDesc& r = cfg.relays[i];
        if (r.role == RelayRole::K1Power) {
            if (!cfg.k1.present || r.channel != cfg.k1.powerChannel || r.lockable) {
                return false;
            }
            foundPower = true;
        } else if (r.role == RelayRole::K1Direction) {
            if (!cfg.k1.present || r.channel != cfg.k1.directionChannel || r.lockable) {
                return false;
            }
            foundDirection = true;
        }
    }
    if (cfg.k1.present && (!foundPower || !foundDirection)) {
        return false;
    }
    return true;
}

bool validateSensors(const HwProjectConfig& cfg) {
    if (cfg.sensorCount > MAX_LOGICAL_SENSORS) {
        return false;
    }
    for (size_t i = 0; i < cfg.sensorCount; ++i) {
        const LogicalSensorDesc& s = cfg.sensors[i];
        if (s.name == nullptr || s.settingKey == nullptr) {
            return false;
        }
        for (size_t j = i + 1; j < cfg.sensorCount; ++j) {
            if (strcmp(s.settingKey, cfg.sensors[j].settingKey) == 0) {
                return false;
            }
        }
    }
    return true;
}

bool antiSeizeChannelBackedByRole(const HwProjectConfig& cfg, uint8_t channel, RelayRole role) {
    for (size_t i = 0; i < cfg.relayCount; ++i) {
        if (cfg.relays[i].channel == channel && cfg.relays[i].role == role) {
            return true;
        }
    }
    return false;
}

bool validateAntiSeize(const HwProjectConfig& cfg) {
    if (cfg.antiSeizeCount > MAX_ANTI_SEIZE_OUTPUTS) {
        return false;
    }
    for (size_t i = 0; i < cfg.antiSeizeCount; ++i) {
        const AntiSeizeOutputDesc& a = cfg.antiSeize[i];
        if (a.enableKey == nullptr) {
            return false;
        }
        for (size_t j = i + 1; j < cfg.antiSeizeCount; ++j) {
            if (strcmp(a.enableKey, cfg.antiSeize[j].enableKey) == 0) {
                return false;
            }
        }

        switch (a.kind) {
            case AntiSeizeKind::Pump:
                if (!antiSeizeChannelBackedByRole(cfg, a.channel, RelayRole::Pump)) {
                    return false;
                }
                break;
            case AntiSeizeKind::Toggle:
                if (!antiSeizeChannelBackedByRole(cfg, a.channel, RelayRole::Diverter)) {
                    return false;
                }
                break;
            case AntiSeizeKind::ValveStroke:
                if (!cfg.k1.present || a.channel != cfg.k1.powerChannel) {
                    return false;
                }
                break;
            default:
                return false;
        }

        if (a.blockWhileOnChannel != NO_RELAY_CHANNEL && !channelConfigured(cfg, a.blockWhileOnChannel)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool validateHwConfig(const HwProjectConfig& cfg) {
    return validateRelays(cfg) && validateK1(cfg) && validateSensors(cfg) && validateAntiSeize(cfg);
}
