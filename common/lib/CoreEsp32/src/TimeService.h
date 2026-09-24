#pragma once
#include <Wire.h>
#include <stddef.h>
#include <stdint.h>
#include "Clock.h"
#include "CommonSettings.h"
#include "CommonState.h"
#include "Ds1307Rtc.h"
#include "EventTypes.h"
#include "TimeKeeper.h"
#include "TzUtil.h"

// ESP32 Clock implementation: DS1307 RTC + SNTP + POSIX TZ (D15-D17). Owns a
// pure TimeKeeper and drives it from RTC reads and SNTP sync results; the SNTP
// callback only stores the synced UTC seconds (no other work), which tick()
// then applies on the loop task. The constructor touches no hardware.
class TimeService : public Clock {
public:
    TimeService() = default;

    // Reads the RTC once via a temporary Ds1307Rtc(wire). On Ok, seeds
    // TimeKeeper::setFromRtc() and the libc wall clock (settimeofday). Always
    // records rtcPresent()/rtcValid() (via fillStatus()). Stores &wire for the
    // later RTC write in tick().
    RtcReadStatus begin(TwoWire& wire);

    // effectiveTz(tz) -> setenv("TZ", ..., 1) + tzset(); records tzValid()
    // (false only if tz itself was implausible and the DEFAULT_TZ fallback had
    // to be used).
    void setTimezone(const char* tz);
    bool tzValid() const { return _tzValid; }

    // Copies server into an internal <=64-byte buffer used by the next
    // configTzTime()/sntp_restart().
    void setNtpServer(const char* server);

    // Hooks for the network stage (dormant: nothing calls these yet, D17).
    void onNetworkUp();
    void onNetworkDown();

    // Applies a pending NTP sync result if any (TimeKeeper::setFromNtp + RTC
    // write + TimeSynced event), then restarts SNTP when
    // TimeKeeper::ntpSyncDue().
    void tick(EventSink& events);

    void fillStatus(TimeStatus& out) const;

    // "YYYY-MM-DD HH:MM:SS" local time via localtime_r/strftime. False if
    // buf/cap are unusable.
    bool formatLocal(uint32_t utc, char* buf, size_t cap) const;

    uint64_t monoMs() const override;  // esp_timer_get_time() / 1000
    Timestamp now() const override;

private:
    static void onSntpSync(struct timeval* tv);

    TwoWire* _wire = nullptr;
    TimeKeeper _keeper;

    bool _rtcPresent = false;
    bool _rtcValid = false;

    bool _tzValid = true;
    // Populated by setTimezone(); empty until then (setTimezone() is always
    // called during CoreServices::begin(), before onNetworkUp() can fire).
    char _tz[TZ_MAX_LEN + 1] = {};

    static constexpr size_t NTP_SERVER_MAX_LEN = 64;
    char _ntpServer[NTP_SERVER_MAX_LEN + 1] = "pool.ntp.org";

    bool _sntpStarted = false;
};
