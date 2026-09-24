#include "EspotaService.h"
#include <Arduino.h>
#include <CommonState.h>
#include <ESPmDNS.h>
#include <Watchdog.h>

void EspotaService::start(
    const char* hostname, const char* password, NetSignals& signals, StartHook onStartHook, void* ctx) {
    if (_started) {
        return;
    }
    _hostname = hostname;
    _signals = &signals;
    _hook = onStartHook;
    _hookCtx = ctx;
    _configured = true;

    // Fail closed like AdminAuth: ArduinoOTA with an empty password accepts
    // unauthenticated uploads. Stay stopped; a later restart() with a
    // non-empty password starts the service (review-9 Should-fix 2).
    if (password == nullptr || password[0] == '\0') {
        if (!_loggedNoPassword) {
            Serial.println("[net] espota disabled: empty admin password");
            _loggedNoPassword = true;
        }
        return;
    }
    _loggedNoPassword = false;

    _ota.emplace();
    ArduinoOTAClass& ota = *_ota;
    ota.setPort(ESPOTA_PORT);
    ota.setHostname(hostname);
    ota.setPassword(password);
    ota.setMdnsEnabled(false);  // EspWifiPort owns MDNS; advertised below
    ota.setRebootOnSuccess(false);  // ConnectivityRuntime requests the reboot (D14)
    ota.onStart([this]() {
        // Fires only after this service's own Update.begin() succeeded.
        _sessionActive = true;
        _signals->espotaActive.store(true);  // web /ota/* answers 409 from now on
        _signals->otaStarted.store(static_cast<uint8_t>(OtaSource::Espota));
        if (_hook != nullptr) {
            _hook(_hookCtx);  // relays OFF + port written before the blocking transfer
        }
    });
    ota.onProgress([this](unsigned int, unsigned int) {
        Watchdog::feed();  // handle() blocks the loop task for the whole upload
        _signals->otaProgress.fetch_add(1);
    });
    // End/failure are reported only for a session this service started:
    // OTA_BEGIN_ERROR (e.g. a web OTA already owns Update) fires without
    // onStart and must not end the other source's session (review-9
    // Should-fix 3). The flag also swallows ArduinoOTA's second error callback
    // after OTA_CONNECT_ERROR (it falls through to Update.end()).
    ota.onEnd([this]() {
        if (_sessionActive) {
            _sessionActive = false;
            _signals->otaEnded.store(1);
        }
    });
    ota.onError([this](ota_error_t) {
        if (_sessionActive) {
            _sessionActive = false;
            _signals->otaEnded.store(2);
        }
    });
    ota.begin();
    // Advertised once: MDNS is begun once and never ended (EspWifiPort), so
    // re-adding the service on every restart() only logs an mDNS error.
    if (!_mdnsAdvertised) {
        MDNS.enableArduino(ESPOTA_PORT, true);
        _mdnsAdvertised = true;
    }
    _started = true;
}

void EspotaService::restart(const char* password) {
    if (!_configured) {
        return;  // start() never ran (Wi-Fi not up yet): it will pick up the password
    }
    if (_started) {
        _ota->end();
        _ota.reset();
        _started = false;
    }
    start(_hostname, password, *_signals, _hook, _hookCtx);
}

void EspotaService::handle() {
    if (!_started) {
        return;
    }
    _ota->handle();  // a whole espota session (begin..end/error) runs inside this call
    // Update has been ended/aborted by the time handle() returns, so only now
    // may the web side use it again (clearing in onError would reopen /ota/*
    // while ArduinoOTA still calls Update.end() after OTA_CONNECT_ERROR).
    _sessionActive = false;
    _signals->espotaActive.store(false);
}
