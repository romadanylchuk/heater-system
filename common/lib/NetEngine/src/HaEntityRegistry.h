#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include <ConfigEngine.h>
#include <HwConfig.h>

// Generic Home Assistant entity model (D8/D9): built once, at startup, from a
// controller's settings schema, its HwProjectConfig, and any project-specific
// custom entity table. Read-only after build(), so it is safe to read from
// other tasks (findByKey/isCommandable/formatState/displayName take no lock).
// Pure: no MQTT/JSON/network includes; HaDiscovery/MqttPublisher (later
// phases) turn entities into topics/payloads.
enum class HaComponent : uint8_t { Sensor, BinarySensor, Switch, Number };
enum class HaSource : uint8_t { Setting, LogicalSensor, Relay, Custom };

// false = unavailable (formatState writes "None").
using HaStateFn = bool (*)(const CommonState& state, char* out, size_t cap);

// Project custom entity tables (stages 07/08): Sensor/BinarySensor only, a
// fixed key/name/classification plus a state function. Reserved and empty for
// stage 04 except for HA_COMMON_ENTITIES (below).
struct HaCustomEntity {
    const char* key;             // [a-z0-9_], <= HA_KEY_MAX_LEN, taken verbatim (no snake conversion)
    const char* name;            // English display name
    HaComponent component;
    const char* unit;            // nullable
    const char* deviceClass;     // nullable
    const char* stateClass;      // nullable
    const char* entityCategory;  // nullable ("diagnostic")
    HaStateFn state;
};

constexpr size_t HA_KEY_MAX_LEN = 31;
constexpr size_t HA_STATE_MAX_LEN = 15;
constexpr size_t HA_MAX_ENTITIES = 96;

struct HaEntity {
    HaComponent component;
    HaSource source;
    uint16_t ref;  // setting index | logical sensor index | relay-table index | 0 (Custom)
    bool configCategory;          // entity_category: config (Number always; Bool settings without HA_SWITCH)
    const HaCustomEntity* custom;  // Custom only, else nullptr
    char key[HA_KEY_MAX_LEN + 1];
};

// Common entities every project gets (D8): Wi-Fi RSSI.
extern const HaCustomEntity HA_COMMON_ENTITIES[];
extern const size_t HA_COMMON_ENTITY_COUNT;

enum class HaRegistryStatus : uint8_t { Ok, TooManyEntities, DuplicateKey, BadKey };

class HaEntityRegistry {
public:
    // Build order: settings (schema order, D8 selection) -> logical sensors
    // (temp_<snake(name)>) -> relays (relay_<snake(name)>) ->
    // HA_COMMON_ENTITIES -> custom. On a non-Ok result, count() == 0.
    HaRegistryStatus build(const ConfigEngine& config, const HwProjectConfig& hw, const HaCustomEntity* custom,
        size_t customCount);

    size_t count() const { return _count; }
    const HaEntity& entity(size_t i) const { return _entities[i]; }

    // -1 if not found. key is not NUL-terminated (a length is given).
    int findByKey(const char* key, size_t len) const;

    bool isCommandable(size_t i) const {
        return _entities[i].component == HaComponent::Switch || _entities[i].component == HaComponent::Number;
    }

    // false => unavailable, writes "None" (D9).
    bool formatState(size_t i, const CommonState& s, const ConfigEngine& c, char* out, size_t cap) const;

    const char* displayName(size_t i, const ConfigEngine& c, char* buf, size_t cap) const;

    const HwProjectConfig* hw() const { return _hw; }

private:
    bool insert(HaComponent comp, HaSource src, uint16_t ref, bool configCat, const HaCustomEntity* custom,
        const char* key);
    bool addCustomEntity(const HaCustomEntity* custom);

    HaEntity _entities[HA_MAX_ENTITIES] = {};
    size_t _count = 0;
    const HwProjectConfig* _hw = nullptr;
    HaRegistryStatus _status = HaRegistryStatus::Ok;
};

// D9: Int -> 0 decimals; Float step >= 1 -> 0, >= 0.1 -> 1, >= 0.01 -> 2, else 3.
size_t haDecimalsForStep(float step);
