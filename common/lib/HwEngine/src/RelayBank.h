#pragma once
#include <stddef.h>
#include <stdint.h>
#include <optional>
#include <BoardConfig.h>
#include <CommonState.h>
#include <EventTypes.h>
#include <HardwareStatus.h>
#include "HwConfig.h"

// Relay layer (D5-D10): per-channel arbitration between three request slots
// (safety > exercise > control), the shared min-ON/OFF lock, the safety bypass,
// RelayChanged/RelayLockDelay logging and the PCF8574 output byte. Pure logic,
// no hardware access -- HwRuntime drives update()/outputByte() against a
// RelayPort. Channels not present in the configure() table are "unused":
// requests are rejected (false) and they stay OFF forever.
class RelayBank {
public:
    // Every configured channel starts actual = requested = OFF, with the lock
    // window starting at bootMs -- boot counts as an OFF transition (D6), so a
    // lockable channel cannot switch ON in under lockMs after a reboot.
    void configure(const RelayChannelDesc* channels, size_t count, uint64_t bootMs);

    // Takes effect at the next update() (D7).
    void setLockMs(uint32_t lockMs) { _lockMs = lockMs; }

    // Controller slot (level: persists until changed). Default OFF. Returns
    // false if the channel is not configured.
    bool requestControl(uint8_t channel, bool on, RelayReason reason = RelayReason::Control);

    // Safety slot: bypasses the lock while set. Returns false if unused.
    bool requestSafety(uint8_t channel, bool on);
    void clearSafety(uint8_t channel);

    // Exercise slot (anti-seize only). std::nullopt releases it. Returns false
    // if unused.
    bool setExercise(uint8_t channel, std::optional<bool> on);

    // Arbitration: effective = safety ?: exercise ?: control. Applies the lock
    // to lockable channels unless the effective request comes from the safety
    // slot. Logs RelayChanged / RelayLockDelay (D8/D9).
    void update(uint64_t nowMs, EventSink& events);

    bool configured(uint8_t channel) const;
    bool actual(uint8_t channel) const;
    bool requested(uint8_t channel) const;  // effective request
    bool safetyActive(uint8_t channel) const;
    bool lockDelayed(uint8_t channel) const;
    uint32_t lockRemainingMs(uint8_t channel, uint64_t nowMs) const;

    // Timestamps for anti-seize "last run" (monotonic ms). lastOnMs = nowMs of
    // the last update() with actual ON (or the OFF-switch time otherwise, so it
    // also tracks "last touched"); hasBeenOn false until the first ON.
    bool hasBeenOn(uint8_t channel) const;
    uint64_t lastOnMs(uint8_t channel) const;
    bool hasChanged(uint8_t channel) const;  // any actual change since configure
    uint64_t lastChangeMs(uint8_t channel) const;
    RelayReason lastReason(uint8_t channel) const;

    // Byte for the PCF8574: bit i = channel i (i < RELAY_CHANNEL_COUNT); unused
    // channels (including P6/P7, which are never in the descriptor table)
    // inactive; inverted when activeLow.
    uint8_t outputByte(bool activeLow) const;

    void fillStatus(RelayArray& out, uint64_t nowMs) const;

private:
    struct Slot {
        bool configured = false;
        bool lockable = false;
        bool logChanges = false;

        bool control = false;
        RelayReason controlReason = RelayReason::Control;

        bool safetySet = false;
        bool safety = false;

        bool exerciseSet = false;
        bool exercise = false;

        bool actual = false;

        uint64_t lastChangeMs = 0;
        bool hasChanged = false;
        bool hasBeenOn = false;
        uint64_t lastOnMs = 0;

        RelayReason lastReason = RelayReason::Boot;

        bool delayLogged = false;
        bool lockDelayed = false;
    };

    struct Effective {
        bool on;
        RelayReason source;
    };

    Slot _slots[RELAY_CHANNEL_COUNT];
    uint32_t _lockMs = 0;

    Slot* slotFor(uint8_t channel);
    const Slot* slotFor(uint8_t channel) const;
    static Effective effectiveOf(const Slot& s);
};
