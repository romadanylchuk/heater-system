#pragma once
#include <stddef.h>
#include <stdint.h>
#include <EventTypes.h>
#include <HardwareStatus.h>
#include "HwConfig.h"
#include "K1Driver.h"
#include "LocalTime.h"
#include "RelayBank.h"

// Anti-seize scheduler (D19-D20): periodically exercises idle pumps/valves so
// they don't seize. "Last run" per output is the shared P3-style last-run
// timer (boiler-room-p3-supply.md, anti-seize.md): it is derived every tick
// from RelayBank/K1Driver's own activity timestamps (RelayBank::lastOnMs for
// a Pump, RelayBank::lastChangeMs for a Toggle, K1Driver::lastMoveMs for the
// ValveStroke), so ANY run -- controller, safety, anti-freeze, or anti-seize
// itself -- resets the idle clock, nothing can be missed between ticks, and
// no separate "who ran it" bookkeeping is needed. Pure logic; local time is
// injected (D21); no NVS persistence of last-run timestamps (D19: timers
// restart at boot).
struct AntiSeizeSettings {
    uint16_t intervalDays;
    uint16_t startMinute;
    uint16_t durationS;
    bool enabled[MAX_ANTI_SEIZE_OUTPUTS];
};

class AntiSeizeScheduler {
public:
    // The daily-checkpoint slack ("ran at 10:00:30, seven days later at
    // 10:00:00" still counts as 7 days) and the safety net that guarantees a
    // run within interval + 24h regardless of clock-valid/invalid switches
    // (D19).
    static constexpr uint64_t CHECK_SLACK_MS = 12ULL * 3600 * 1000;
    static constexpr uint64_t SAFETY_NET_MS = 24ULL * 3600 * 1000;
    // 120 s travel + 10 %; stage 08 overrides via setK1StrokeMs() with the
    // real calibrated value.
    static constexpr uint32_t K1_DEFAULT_STROKE_MS = 132000;

    AntiSeizeScheduler(RelayBank& relays, K1Driver& k1, EventSink& events);

    // Every configured output starts idle (pending=false, running=false) with
    // lastRun = bootMs (boot counts as "just ran", consistent with
    // RelayBank's own D6 boot-lock convention).
    void configure(const AntiSeizeOutputDesc* outputs, size_t count, uint64_t bootMs);
    void setSettings(const AntiSeizeSettings& settings);
    void setK1StrokeMs(uint32_t strokeMs);
    // Controller hook (stages 07/08): default false. Releasing an inhibited,
    // still-pending output lets it start at the next tick.
    void setInhibited(size_t output, bool inhibited);

    void tick(uint64_t nowMs, const LocalTimeInfo& local);

    uint64_t lastRunMs(size_t output) const;
    bool pending(size_t output) const;
    bool running(size_t output) const;

    void fillStatus(AntiSeizeStatus& out, uint64_t nowMs) const;

private:
    enum class Phase : uint8_t { None, WaitOn, Hold, K1Open, K1Close };

    struct Output {
        AntiSeizeOutputDesc desc{};
        bool configured = false;

        uint64_t lastRun = 0;
        bool pending = false;
        uint64_t pendingSinceMs = 0;

        bool running = false;
        Phase phase = Phase::None;
        uint64_t runStartMs = 0;   // when this run started (Pump/Toggle timeout base)
        uint64_t holdStartMs = 0;  // when the Hold phase (target reached) started
        bool toggleTarget = false;

        bool inhibited = false;
    };

    RelayBank& _relays;
    K1Driver& _k1;
    EventSink& _events;

    Output _outputs[MAX_ANTI_SEIZE_OUTPUTS];
    size_t _count = 0;

    AntiSeizeSettings _settings{};
    uint32_t _strokeMs = K1_DEFAULT_STROKE_MS;
    uint32_t _lastCheckDay = 0;  // 0 = no checkpoint processed yet

    uint64_t activityOf(const AntiSeizeOutputDesc& desc) const;
    bool isBlocked(const Output& out) const;
    void startOutput(Output& out, size_t idx, uint64_t nowMs);
    void advanceRunning(Output& out, size_t idx, uint64_t nowMs);
    void advanceValveStroke(Output& out, size_t idx, uint64_t nowMs);
};
