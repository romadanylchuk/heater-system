#pragma once
#include <freertos/FreeRTOS.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <Command.h>
#include <CommandResultBoard.h>
#include <CommonState.h>
#include <ConfigSchema.h>
#include <ConnectivityServices.h>
#include <CoreServices.h>
#include <EventLog.h>
#include <ExportSlot.h>
#include <HwConfig.h>
#include <WebJson.h>
#include <WebServerHost.h>
#include "CaptivePortal.h"
#include "SessionGate.h"
#include "SharedJsonBuffer.h"

// Stage-05 web facade (D1/D8/D18), constructed next to ConnectivityServices
// in both main.cpp files. The constructor touches nothing. attach() (in
// setup(), before net.begin()) injects the SessionGate and the two-phase
// route installer into ConnectivityServices and the command-result hook into
// CoreServices; WebServerHost::begin then calls install() from inside its
// registration order.
//
// Threading: tick()/fastTick()/onCommandResult() run on the loop task and are
// the only code that reads CommonState/ConfigEngine/EventLog: they rebuild
// the JSON documents into SharedJsonBuffers. HTTP handlers (AsyncTCP task)
// only touch the SharedJsonBuffers (mutex), the SessionGate and the
// CommandResultBoard/ExportSlot (portMUX _mux), the atomics and the command
// queue. Nothing here blocks the loop task.
//
// Single instance: the JSON documents live in file-scope static arrays in
// WebServices.cpp (WEB_*_CAP), not in the object.
constexpr size_t WEB_STATE_CAP = 3072;
constexpr size_t WEB_SENSORS_CAP = 2048;
constexpr size_t WEB_CONFIG_CAP = 4096;
// A full EventLog of max-width entries must fit (review-4 Must-fix); anything
// wider is truncated by buildLogJson (oldest dropped, "truncated":true).
constexpr size_t WEB_LOG_CAP = webLogCapFor(EventLog::CAPACITY);
constexpr size_t WEB_SCRATCH_CAP = WEB_LOG_CAP;   // loop-task rebuild scratch (>= every cap above)
static_assert(WEB_LOG_CAP >= EventLog::CAPACITY * WEB_LOG_ENTRY_MAX + WEB_LOG_FRAME,
    "the /api/log document must hold a full event log of max-width entries");

class WebServices {
public:
    WebServices(CommonState& state, CoreServices& core, ConnectivityServices& net, const ConfigSchema& schema,
        const HwProjectConfig& hwCfg);

    void attach();     // setup(), BEFORE net.begin()
    void tick();       // ~1 s, after net.tick(): snapshots, backup export, flags, captive start/stop
    void fastTick();   // ~100 ms, after net.fastTick(): captive DNS poll

private:
    static constexpr uint64_t LOG_REBUILD_MS = 60000;

    static void install(AsyncWebServer& server, WebInstallPhase phase, void* ctx);
    static void onCommandResult(const Command& cmd, CommandStatus st, void* ctx);   // loop task
    static bool formatLocal(uint32_t utc, char* out, size_t cap, void* ctx);        // core.time().formatLocal
    static uint64_t nowMs();

    void installEarly(AsyncWebServer& s);   // body-size guards (WebApiWrite.cpp)
    void installRead(AsyncWebServer& s);    // WebApiRoutes.cpp
    void installWrite(AsyncWebServer& s);   // state-changing routes (WebApiWrite.cpp)
    void installBackupImport(AsyncWebServer& s);   // raw JSON body route (WebApiWrite.cpp)
    void installStatic(AsyncWebServer& s);  // "/" + serveStatic + final catch-all (WebApiRoutes.cpp)

    uint32_t nextCmdId();                   // WEB_CMD_ID_BASE | n, never 0
    bool postCommand(const Command& c);     // board.expect under _mux, then queue.post
    // Posts c and answers 202 {"ok":true,"id":N} or 503 {"ok":false,"error":"busy"}.
    void postAndAnswer(AsyncWebServerRequest* r, const Command& c);

    // Any task: _otaBusy, an unacknowledged web OTA start, or an espota
    // transfer (review-4 suggestion).
    bool otaBusyNow() const;
    void exportTick(uint64_t now);   // loop task: build a requested backup, expire an unclaimed one
    void latchWebImageAfterOta();    // loop task: see _webImageLatched
    void rebuildSnapshots();
    // update() + one "[web]" line on the ok -> failed edge of each document.
    bool publish(SharedJsonBuffer& buf, size_t len, const char* name, bool& failed);
    bool logNeedsRebuild(uint64_t now) const;

    CommonState& _state;
    CoreServices& _core;
    ConnectivityServices& _net;
    const ConfigSchema& _schema;
    const HwProjectConfig& _hwCfg;

    SessionGate _gate;
    SharedJsonBuffer _stateJson;
    SharedJsonBuffer _sensorsJson;
    SharedJsonBuffer _configJson;
    SharedJsonBuffer _logJson;
    CaptivePortal _captive;

    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;   // _board + _export
    CommandResultBoard _board;
    ExportSlot _export;

    std::atomic<uint32_t> _cmdCounter{0};
    std::atomic<bool> _webImage{false};
    std::atomic<bool> _otaBusy{false};
    // Web OTA start count (NetSignals::webOtaStarts, bumped in ElegantOTA's
    // onStart on the AsyncTCP task) already reflected in _otaBusy. Static
    // serving is off while the live count differs, so a filesystem OTA never
    // races LittleFS reads during the up-to-one-tick lag of _otaBusy
    // (review-4 Should-fix 1).
    std::atomic<uint32_t> _otaStartsAcked{0};
    std::atomic<bool> _installed{false};

    // Loop task only.
    uint32_t _lastLogSeq = 0;
    bool _lastLogTimeValid = false;
    bool _logBuilt = false;
    uint64_t _lastLogBuildMs = 0;
    uint32_t _otaStartsPrev = 0;   // count read at the previous tick()
    // Once a filesystem-mode web OTA has started and is over without a
    // reboot (failed, aborted, or a success waiting for its reboot), the
    // LittleFS partition may be half-rewritten: _webImage is cleared for
    // good, so "/" serves the rescue page and static serving stays off until
    // the next boot (review-4 suggestion). A firmware-only web OTA never
    // latches (review-5 suggestion).
    bool _webImageLatched = false;
    bool _exportFailLogged = false;   // export-failure message printed; reset on success
    bool _stateFailed = false;
    bool _sensorsFailed = false;
    bool _configFailed = false;
    bool _logFailed = false;
};
