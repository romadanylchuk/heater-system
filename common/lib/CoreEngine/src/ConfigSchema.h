#pragma once
#include <stddef.h>
#include <stdint.h>
#include "SettingDescriptor.h"

// A single logical/NVS key rename applied when migrating across a config version
// boundary (D7). oldKey/newKey are the backup-facing logical keys; oldNvsKey/newNvsKey
// are the NVS-facing keys actually renamed in storage.
struct KeyRename {
    const char* oldKey;
    const char* newKey;
    const char* oldNvsKey;
    const char* newNvsKey;
};

// One migration step: renames applied when the stored config version is below
// toVersion. A schema's migrations are ordered by ascending toVersion.
struct ConfigMigration {
    uint16_t toVersion;
    const KeyRename* renames;
    size_t renameCount;
};

// A controller's full settings schema. tables[0] must always be COMMON_SETTINGS_TABLE
// (CommonSettings.h); controller-specific tables follow it (D4).
struct ConfigSchema {
    const char* controllerType;  // "boiler-room" | "home-heating"
    uint16_t configVersion;      // >= 1
    const SettingsTable* tables;
    size_t tableCount;
    const ConfigMigration* migrations;
    size_t migrationCount;
};

constexpr const char* CONFIG_VERSION_KEY = "cfgVer";
