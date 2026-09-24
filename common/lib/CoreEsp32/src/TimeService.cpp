#include "TimeService.h"
#include <Arduino.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace {
// Only one TimeService exists per firmware (owned by CoreServices), so a
// file-scope critical section + pending-sync state is sufficient to bridge the
// lwIP-task SNTP callback to the loop-task tick().
portMUX_TYPE g_syncMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_syncPending = false;
volatile uint32_t g_syncUtc = 0;
}  // namespace

void TimeService::onSntpSync(struct timeval* tv) {
    // Runs on the lwIP task. No other work here (per D17) -- just record the
    // synced UTC seconds for tick() to pick up on the loop task.
    portENTER_CRITICAL(&g_syncMux);
    g_syncUtc = static_cast<uint32_t>(tv->tv_sec);
    g_syncPending = true;
    portEXIT_CRITICAL(&g_syncMux);
}

RtcReadStatus TimeService::begin(TwoWire& wire) {
    _wire = &wire;

    Ds1307Rtc rtc(wire);
    uint32_t utc = 0;
    RtcReadStatus status = rtc.read(utc);

    _rtcPresent = status != RtcReadStatus::Missing;
    _rtcValid = status == RtcReadStatus::Ok;

    if (status == RtcReadStatus::Ok) {
        _keeper.setFromRtc(utc, monoMs());

        struct timeval tv {};
        tv.tv_sec = static_cast<time_t>(utc);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
    }

    return status;
}

void TimeService::setTimezone(const char* tz) {
    _tzValid = isPlausiblePosixTz(tz);
    const char* effective = effectiveTz(tz);
    strncpy(_tz, effective, TZ_MAX_LEN);
    _tz[TZ_MAX_LEN] = '\0';

    setenv("TZ", _tz, 1);
    tzset();
}

void TimeService::setNtpServer(const char* server) {
    if (server == nullptr) {
        return;
    }
    strncpy(_ntpServer, server, NTP_SERVER_MAX_LEN);
    _ntpServer[NTP_SERVER_MAX_LEN] = '\0';
}

void TimeService::onNetworkUp() {
    const uint64_t mono = monoMs();
    _keeper.onNetworkUp(mono);

    if (!_sntpStarted) {
        sntp_set_time_sync_notification_cb(&TimeService::onSntpSync);
        configTzTime(_tz, _ntpServer);
        _sntpStarted = true;
        _keeper.markNtpAttempt(mono);
    }
    // On later calls the sync becomes due (TimeKeeper::ntpSyncDue) and tick() handles it.
}

void TimeService::onNetworkDown() {
    _keeper.onNetworkDown();
}

void TimeService::tick(EventSink& events) {
    const uint64_t mono = monoMs();

    bool pending = false;
    uint32_t syncedUtc = 0;
    portENTER_CRITICAL(&g_syncMux);
    if (g_syncPending) {
        pending = true;
        syncedUtc = g_syncUtc;
        g_syncPending = false;
    }
    portEXIT_CRITICAL(&g_syncMux);

    if (pending) {
        _keeper.setFromNtp(syncedUtc, mono);

        bool rtcWriteOk = false;
        if (_wire != nullptr) {
            Ds1307Rtc rtc(*_wire);
            rtcWriteOk = rtc.write(syncedUtc);
            if (rtcWriteOk) {
                _rtcPresent = true;
                _rtcValid = true;
            } else {
                _rtcPresent = false;
            }
        }

        events.logEvent(toU16(EventType::TimeSynced), EVENT_SOURCE_NTP, rtcWriteOk ? 1.0f : 0.0f, 0.0f,
            EventReason::Ntp);
    }

    if (_sntpStarted && _keeper.ntpSyncDue(mono)) {
        sntp_restart();
        _keeper.markNtpAttempt(mono);
    }
}

void TimeService::fillStatus(TimeStatus& out) const {
    const Timestamp ts = now();
    out.utcNow = ts.seconds;
    out.valid = ts.realTime;
    out.source = _keeper.source();
    out.rtcPresent = _rtcPresent;
    out.rtcValid = _rtcValid;
    out.lastNtpSyncUtc = _keeper.lastNtpSyncUtc();
}

bool TimeService::formatLocal(uint32_t utc, char* buf, size_t cap) const {
    if (buf == nullptr || cap == 0) {
        return false;
    }
    const time_t t = static_cast<time_t>(utc);
    struct tm tmInfo {};
    localtime_r(&t, &tmInfo);
    return strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tmInfo) > 0;
}

uint64_t TimeService::monoMs() const {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

Timestamp TimeService::now() const {
    return _keeper.now(monoMs());
}
