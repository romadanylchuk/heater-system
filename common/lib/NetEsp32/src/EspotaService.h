#pragma once
#include <ArduinoOTA.h>
#include <optional>
#include <NetSignals.h>

// Password-protected espota (ArduinoOTA) with a runtime-changeable password
// (D14/D16). ArduinoOTAClass::setPassword() only takes effect while the
// instance is uninitialised and has no password yet, so a password change
// destroys and re-creates the instance (end(), reset(), emplace, begin()).
//
// Threading: everything, including the callbacks, runs on the loop task --
// handle() is called from ConnectivityServices::fastTick() and BLOCKS for the
// whole upload once a session starts (espota's own documented behaviour). So:
//  - onStart posts the OtaSource::Espota start signal and calls the owner's
//    start hook, which inhibits the relays and writes the port immediately
//    (the loop cannot run hw.fastTick() until the upload ends);
//  - onProgress feeds the task watchdog;
//  - NetSignals::espotaActive is true from onStart until handle() returns,
//    so the web server's /ota/* guard answers 409 meanwhile;
//  - restart() runs on the same task, so it can never overlap a session.
// The global `ArduinoOTA` instance is not used.
class EspotaService {
public:
    static constexpr uint16_t ESPOTA_PORT = 3232;
    using StartHook = void (*)(void* ctx);

    // Creates, configures and begins the service (port 3232, hostname,
    // password, mDNS advertising via MDNS.enableArduino instead of
    // ArduinoOTA's own MDNS.begin, reboot-on-success off). hostname must
    // outlive the service (static NetIdentity string).
    // Refuses to start (fail closed, logged once) while password is null or
    // empty; the configuration is kept so restart() can start it later.
    void start(const char* hostname, const char* password, NetSignals& signals, StartHook onStartHook, void* ctx);

    // New password for later sessions (stops/re-creates, or starts a service
    // that start() refused for an empty password); no-op until start() has
    // been called at least once.
    void restart(const char* password);

    // Caller skips this while a web OTA session is active (the facade checks
    // ConnectivityRuntime::activeOtaSource()), so espota never races the web
    // upload for the global Update object.
    void handle();
    bool started() const { return _started; }

private:
    std::optional<ArduinoOTAClass> _ota;
    bool _started = false;
    bool _configured = false;        // start() called at least once (hostname/signals/hook set)
    bool _loggedNoPassword = false;  // "disabled: empty password" logged once per refusal streak
    bool _mdnsAdvertised = false;    // "_arduino._tcp" service added (once; survives restart())
    bool _sessionActive = false;     // own onStart fired; end/error are reported only then
    const char* _hostname = nullptr;
    NetSignals* _signals = nullptr;
    StartHook _hook = nullptr;
    void* _hookCtx = nullptr;
};
