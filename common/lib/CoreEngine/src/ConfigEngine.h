#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ConfigSchema.h"
#include "EventTypes.h"
#include "KvStore.h"
#include "SettingDescriptor.h"

// Descriptor-driven settings engine: load from NVS (via KvStore), clamp, debounced
// save, version migration/downgrade, factory reset and config-change logging (via
// EventSink). One instance per controller, constructed over the "cfg" namespace
// store and the shared EventLog. Pure/native-testable (D1/D3-D9).
enum class ConfigStatus : uint8_t {
    Ok,
    Clamped,
    Unchanged,
    Rejected,
    InvalidIndex,
    TypeMismatch,
    StoreError,
    SchemaInvalid,
};

constexpr size_t CONFIG_MAX_SETTINGS = 128;
constexpr size_t CONFIG_STRING_POOL_BYTES = 2048;
constexpr uint32_t CONFIG_SAVE_DEBOUNCE_MS = 2000;

using SettingChangeHook = void (*)(size_t index, void* ctx);

class ConfigEngine {
public:
    ConfigEngine(KvStore& store, EventSink& events);

    // Validates the schema, lays out the string pool, sets defaults, then loads from
    // the store (migration/downgrade, per-setting clamp, debounce-free immediate
    // write-back of anything the load changed). SchemaInvalid leaves the engine
    // unusable (isReady() == false); StoreError leaves it usable on defaults/RAM.
    ConfigStatus begin(const ConfigSchema& schema, uint64_t monoMs);

    bool isReady() const { return _ready; }
    const ConfigSchema& schema() const { return *_schema; }
    size_t count() const { return _count; }

    const SettingDescriptor* descriptor(size_t index) const;  // nullptr if out of range
    int indexOf(const char* key) const;                       // -1 if unknown

    float getNumber(size_t index) const;      // Int/Float/Bool; 0 for Text/invalid
    int32_t getInt(size_t index) const;
    bool getBool(size_t index) const;
    const char* getText(size_t index) const;   // "" for non-Text/invalid

    ConfigStatus setNumber(size_t index, float value, EventReason origin, uint64_t monoMs);
    ConfigStatus setText(size_t index, const char* value, EventReason origin, uint64_t monoMs);

    ConfigStatus tick(uint64_t monoMs);  // commits dirty values once >= CONFIG_SAVE_DEBOUNCE_MS since last change
    ConfigStatus flushNow();             // immediate write+commit of dirty values
    ConfigStatus factoryReset();         // erase namespace, defaults, write cfgVer, commit; logs nothing itself

    bool isDirty() const { return _anyDirty; }
    bool lastStoreOk() const { return _lastStoreOk; }
    uint16_t storedVersionAtBoot() const { return _storedVersionAtBoot; }  // 0 if absent

    void setChangeHook(SettingChangeHook hook, void* ctx) {
        _hook = hook;
        _hookCtx = ctx;
    }

    // Pure schema check: controllerType/configVersion, tables[0] == common table (by
    // content), capacity, key/nvsKey lengths and uniqueness, numeric bounds, text
    // bounds/pool budget, and migration ordering/target-key existence. Used by
    // begin() and directly by tests.
    static bool validateSchema(const ConfigSchema& schema);

private:
    void logConfigEvent(EventType type, uint16_t source, float value, float aux, EventReason reason);
    void logSettingEvent(EventType type, size_t index, float value, float aux, EventReason reason);
    void runMigrations(const ConfigSchema& schema, uint16_t storedVersion);
    void applyRename(const KeyRename& rename);
    void setStringValue(size_t index, const char* value);
    void loadFromStore(uint64_t monoMs);

    KvStore& _store;
    EventSink& _events;
    const ConfigSchema* _schema = nullptr;
    bool _ready = false;
    size_t _count = 0;

    const SettingDescriptor* _descriptors[CONFIG_MAX_SETTINGS] = {};
    float _numbers[CONFIG_MAX_SETTINGS] = {};
    size_t _stringOffset[CONFIG_MAX_SETTINGS] = {};
    bool _dirty[CONFIG_MAX_SETTINGS] = {};
    char _stringPool[CONFIG_STRING_POOL_BYTES] = {};

    bool _anyDirty = false;
    uint64_t _lastChangeMs = 0;
    bool _lastStoreOk = true;
    uint16_t _storedVersionAtBoot = 0;

    SettingChangeHook _hook = nullptr;
    void* _hookCtx = nullptr;
};
