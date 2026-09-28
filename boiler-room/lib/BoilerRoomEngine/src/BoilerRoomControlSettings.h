#pragma once
#include <stddef.h>
#include <stdint.h>
#include <SettingDescriptor.h>

// boiler-room controller settings (stage 07, C2): P1/overheat, P2, P3 offer,
// anti-freeze and accumulator energy. Appended LAST (tables[4]) to
// BOILER_ROOM_SCHEMA so every earlier index stays put (additive, config version
// unchanged). Key == nvsKey (<= 15 chars) in every row. Ranges not given by the
// architecture were chosen by the planner (D9); defaults are the architecture's.
constexpr const char* BR_KEY_P1_DELTA_ON = "p1DeltaOn";
constexpr const char* BR_KEY_P1_DELTA_OFF = "p1DeltaOff";
constexpr const char* BR_KEY_P1_T1_MIN = "p1T1Min";
constexpr const char* BR_KEY_P1_HYST = "p1Hyst";
constexpr const char* BR_KEY_OH_ON = "ohOn";
constexpr const char* BR_KEY_OH_CLEAR = "ohClear";
constexpr const char* BR_KEY_P2_T2_OFF = "p2T2Off";
constexpr const char* BR_KEY_P2_HYST = "p2Hyst";
constexpr const char* BR_KEY_P2_T1_BURN = "p2T1Burn";
constexpr const char* BR_KEY_P3_T3_OFFER = "p3T3Offer";
constexpr const char* BR_KEY_P3_OFFER_WIN = "p3OfferWin";
constexpr const char* BR_KEY_P3_OFFER_WAIT = "p3OfferWait";
constexpr const char* BR_KEY_AF_ENABLE = "afEnable";
constexpr const char* BR_KEY_AF_INTERVAL = "afInterval";
constexpr const char* BR_KEY_AF_DURATION = "afDuration";
constexpr const char* BR_KEY_ACC_VOLUME = "accVolume";
constexpr const char* BR_KEY_ACC_T_BASE = "accTBase";
constexpr const char* BR_KEY_HOME_NO_NEED = "homeNoNeed";  // existing setting (BOILER_ROOM_SETTINGS)

struct BoilerRoomSettings {
    float p1DeltaOn, p1DeltaOff, p1T1Min, p1Hyst;
    float ohOn, ohClear;
    float p2T2Off, p2Hyst, p2T1Burn;
    float p3T3Offer;
    uint32_t p3OfferWindowMin, p3OfferWaitMin;   // wait 0 = no wait
    bool afEnable;
    uint32_t afIntervalMin, afDurationS;
    float accVolumeL, accTBase;
};

inline constexpr SettingDescriptor BOILER_ROOM_CONTROL_SETTINGS[] = {
    floatSetting("p1DeltaOn", "p1DeltaOn", "P1 ON differential (T1 − T3)", "Диференціал увімкнення P1 (T1 − T3)",
        "°C", "p1", 2.0f, 20.0f, 5.0f, 0.5f),
    floatSetting("p1DeltaOff", "p1DeltaOff", "P1 OFF differential (T1 − T3)", "Диференціал вимкнення P1 (T1 − T3)",
        "°C", "p1", 0.0f, 15.0f, 2.0f, 0.5f),
    intSetting("p1T1Min", "p1T1Min", "P1 minimum boiler flow T1", "Мінімальна T1 для P1",
        "°C", "p1", 40, 80, 60),
    floatSetting("p1Hyst", "p1Hyst", "P1 T1_min hysteresis", "Гістерезис T1_min для P1",
        "°C", "p1", 1.0f, 10.0f, 2.0f, 0.5f),
    intSetting("ohOn", "ohOn", "Overheat: force P1 above", "Перегрів: примусово P1 вище",
        "°C", "p1", 80, 95, 90),
    intSetting("ohClear", "ohClear", "Overheat clears below", "Перегрів знімається нижче",
        "°C", "p1", 70, 94, 87),
    intSetting("p2T2Off", "p2T2Off", "P2 OFF at return T2", "P2 вимикається при T2",
        "°C", "p2", 55, 65, 60),
    floatSetting("p2Hyst", "p2Hyst", "P2 hysteresis", "Гістерезис P2",
        "°C", "p2", 1.0f, 5.0f, 3.0f, 0.5f),
    intSetting("p2T1Burn", "p2T1Burn", "Boiler burning above T1", "Котел горить при T1 вище",
        "°C", "p2", 20, 60, 40),
    intSetting("p3T3Offer", "p3T3Offer", "Offer heat at T3", "Пропонувати тепло при T3",
        "°C", "p3", 40, 85, 60),
    intSetting("p3OfferWin", "p3OfferWin", "Offer window", "Вікно пропозиції",
        "min", "p3", 1, 60, 10),
    intSetting("p3OfferWait", "p3OfferWait", "Wait between offers (0 = none)",
        "Пауза між пропозиціями (0 = без паузи)", "min", "p3", 0, 480, 60),
    boolSetting("afEnable", "afEnable", "Anti-freeze enable", "Захист від замерзання", "flags", true,
        SETTING_FLAG_HA_SWITCH),
    intSetting("afInterval", "afInterval", "Anti-freeze: P3 idle interval", "Антизамерзання: інтервал простою P3",
        "min", "p3", 5, 240, 30),
    intSetting("afDuration", "afDuration", "Anti-freeze run time", "Тривалість антизамерзання",
        "s", "p3", 10, 600, 60),
    intSetting("accVolume", "accVolume", "Accumulator volume", "Об'єм акумулятора",
        "L", "accumulator", 100, 2000, 500),
    intSetting("accTBase", "accTBase", "Energy base temperature", "Базова температура енергії",
        "°C", "accumulator", 10, 60, 30),
};
inline constexpr SettingsTable BOILER_ROOM_CONTROL_SETTINGS_TABLE = makeTable(BOILER_ROOM_CONTROL_SETTINGS);

constexpr size_t BOILER_ROOM_CONTROL_SETTING_COUNT = 17;
static_assert(sizeof(BOILER_ROOM_CONTROL_SETTINGS) / sizeof(BOILER_ROOM_CONTROL_SETTINGS[0]) ==
        BOILER_ROOM_CONTROL_SETTING_COUNT,
    "BOILER_ROOM_CONTROL_SETTING_COUNT out of sync with BOILER_ROOM_CONTROL_SETTINGS");

// The descriptor defaults as a settings struct (read from the table rows).
BoilerRoomSettings defaultBoilerRoomSettings();

// Ordering guard (D8), applied every tick before the logic sees the values:
// p1DeltaOff <= p1DeltaOn - 0.5 and ohClear <= ohOn - 1. Everything else is
// returned unchanged.
BoilerRoomSettings guardBoilerRoomSettings(BoilerRoomSettings s);
