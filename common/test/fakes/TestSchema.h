#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../lib/CoreEngine/src/CommonSettings.h"
#include "../../lib/CoreEngine/src/ConfigSchema.h"
#include "../../lib/CoreEngine/src/SettingDescriptor.h"

// Test-only settings tables/schemas for ConfigEngineSuite (stage 02) and BackupSuite
// (stage 02 phase 3). Not shipped in firmware (test-only, common/test/fakes/).
//
// "test-a" v1/v2 share the same table shape except for one Text setting, renamed
// oldTemp/oldTmp -> newTemp/newTmp by the v1->v2 migration. "test-b" v1 has the same
// key names but a different type for testBool (Int instead of Bool), for the backup
// per-field type check in Phase 3.

constexpr size_t TEST_INDEX_INT = COMMON_SETTING_COUNT + 0;
constexpr size_t TEST_INDEX_FLOAT = COMMON_SETTING_COUNT + 1;
constexpr size_t TEST_INDEX_BOOL = COMMON_SETTING_COUNT + 2;
constexpr size_t TEST_INDEX_TEXT = COMMON_SETTING_COUNT + 3;  // oldTemp (v1) / newTemp (v2)
constexpr size_t TEST_INDEX_SECRET = COMMON_SETTING_COUNT + 4;
constexpr size_t TEST_INDEX_NO_BACKUP = COMMON_SETTING_COUNT + 5;

inline constexpr SettingDescriptor TEST_SETTINGS_V1[] = {
    intSetting("testInt", "testInt", "Test int", "Тест int", nullptr, "test", 0, 100, 50),
    floatSetting("testFloat", "testFloat", "Test float", "Тест float", nullptr, "test", -10.5f, 90.0f, 20.0f),
    boolSetting("testBool", "testBool", "Test bool", "Тест bool", "test", false),
    textSetting("oldTemp", "oldTmp", "Test text", "Тест текст", "test", 1, 16, "hi"),
    textSetting("testSecret", "testSecret", "Test secret", "Тест секрет", "test", 0, 32, "", SETTING_FLAG_SECRET),
    intSetting("testNoBackup", "testNoBk", "Test no-backup", "Тест без бекапу", nullptr, "test", 0, 10, 0, 1,
        SETTING_FLAG_NO_BACKUP),
};
inline constexpr SettingsTable TEST_SETTINGS_V1_TABLE = makeTable(TEST_SETTINGS_V1);

inline constexpr SettingDescriptor TEST_SETTINGS_V2[] = {
    intSetting("testInt", "testInt", "Test int", "Тест int", nullptr, "test", 0, 100, 50),
    floatSetting("testFloat", "testFloat", "Test float", "Тест float", nullptr, "test", -10.5f, 90.0f, 20.0f),
    boolSetting("testBool", "testBool", "Test bool", "Тест bool", "test", false),
    textSetting("newTemp", "newTmp", "Test text", "Тест текст", "test", 1, 16, "hi"),
    textSetting("testSecret", "testSecret", "Test secret", "Тест секрет", "test", 0, 32, "", SETTING_FLAG_SECRET),
    intSetting("testNoBackup", "testNoBk", "Test no-backup", "Тест без бекапу", nullptr, "test", 0, 10, 0, 1,
        SETTING_FLAG_NO_BACKUP),
};
inline constexpr SettingsTable TEST_SETTINGS_V2_TABLE = makeTable(TEST_SETTINGS_V2);

inline constexpr KeyRename TEST_RENAMES_V2[] = {
    {"oldTemp", "newTemp", "oldTmp", "newTmp"},
};
inline constexpr ConfigMigration TEST_MIGRATIONS_V2[] = {
    {2, TEST_RENAMES_V2, 1},
};

inline constexpr SettingsTable TEST_SCHEMA_A_V1_TABLES[] = {COMMON_SETTINGS_TABLE, TEST_SETTINGS_V1_TABLE};
inline constexpr ConfigSchema TEST_SCHEMA_A_V1 = {"test-a", 1, TEST_SCHEMA_A_V1_TABLES, 2, nullptr, 0};

inline constexpr SettingsTable TEST_SCHEMA_A_V2_TABLES[] = {COMMON_SETTINGS_TABLE, TEST_SETTINGS_V2_TABLE};
inline constexpr ConfigSchema TEST_SCHEMA_A_V2 = {"test-a", 2, TEST_SCHEMA_A_V2_TABLES, 2, TEST_MIGRATIONS_V2, 1};

inline constexpr SettingDescriptor TEST_SETTINGS_B_V1[] = {
    intSetting("testInt", "testInt", "Test int", "Тест int", nullptr, "test", 0, 100, 50),
    floatSetting("testFloat", "testFloat", "Test float", "Тест float", nullptr, "test", -10.5f, 90.0f, 20.0f),
    // Deliberately a different type than "test-a"'s testBool, for the Phase 3 backup
    // per-field type check.
    intSetting("testBool", "testBool", "Test bool as int", "Тест bool як int", nullptr, "test", 0, 1, 0),
    textSetting("oldTemp", "oldTmp", "Test text", "Тест текст", "test", 1, 16, "hi"),
    textSetting("testSecret", "testSecret", "Test secret", "Тест секрет", "test", 0, 32, "", SETTING_FLAG_SECRET),
    intSetting("testNoBackup", "testNoBk", "Test no-backup", "Тест без бекапу", nullptr, "test", 0, 10, 0, 1,
        SETTING_FLAG_NO_BACKUP),
};
inline constexpr SettingsTable TEST_SETTINGS_B_V1_TABLE = makeTable(TEST_SETTINGS_B_V1);
inline constexpr SettingsTable TEST_SCHEMA_B_V1_TABLES[] = {COMMON_SETTINGS_TABLE, TEST_SETTINGS_B_V1_TABLE};
inline constexpr ConfigSchema TEST_SCHEMA_B_V1 = {"test-b", 1, TEST_SCHEMA_B_V1_TABLES, 2, nullptr, 0};
