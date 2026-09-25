#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WebAccess.h"

// The single source of truth for every HTTP route the web layer registers
// (D19). Used both to build AsyncWebServer matchers/filters at runtime
// (routeMatcher/routeMethod, NetEsp32's RequestGate.h) and to validate the
// table natively (validateRouteTable), so a bad route (wrong access level,
// an unterminated prefix, a shadowed exact match) fails a native test
// instead of only showing up on the device.
//
// Only routes actually dispatched through routeMatcher()/routeSpec() live
// in the table returned by routeTable(). ElegantOtaStart, ElegantOtaUpload
// and Static are documentation-only rows: ElegantOTA's own routes and
// serveStatic() are wired directly by WebServerHost/WebServices, not
// through this table, so including them here would make the (deliberately
// registered) OtaAny/UpdateSub guard prefixes look like they "shadow" them.
enum class RouteId : uint8_t {
    // Guards (registered first, Early install phase).
    GuardFormBody,
    GuardImportBody,
    OtaAny,
    UpdatePage,
    UpdateSub,
    // Stage 04.
    WifiStatus,
    WifiScanGet,
    WifiScanPost,
    WifiSave,
    Version,
    SetupPage,
    // Stage 05.
    Login,
    Logout,
    Session,
    State,
    Schema,
    ConfigGet,
    ConfigSet,
    Sensors,
    SensorAssign,
    SensorClear,
    SensorRescan,
    Log,
    CmdResult,
    BackupExportReq,
    BackupExportGet,
    BackupImport,
    FactoryReset,
    OtaGrant,
    Root,
    // Documentation-only (not in routeTable(), not dispatched through
    // routeMatcher/routeSpec): ElegantOTA's internal routes and the final
    // catch-all static handler.
    ElegantOtaStart,
    ElegantOtaUpload,
    Static,
};

// The first id that is a "regular" (non-guard) route. Guards (id below
// this) are exempt from the method/access rules and from the
// uniqueness/shadow checks below, because they are AsyncWebServer filters
// layered in front of a regular route, not a full route match on their
// own (e.g. GuardImportBody intentionally shares its (method,path) with
// BackupImport).
constexpr RouteId FirstRegular = RouteId::WifiStatus;

enum class RouteMatch : uint8_t { Exact, Prefix };
enum class RouteMethod : uint8_t { Get, Post, Any };

struct RouteSpec {
    RouteId id;
    RouteMethod method;
    const char* path;
    RouteMatch match;
    Access access;
    bool stateChangingGet;   // only ElegantOTA's GET /ota/start (documentation-only row)
};

// Fail-closed sentinel for an id that is not in routeTable() (the
// documentation-only ids ElegantOtaStart/ElegantOtaUpload/Static, or a
// corrupt value): strictest access (Ota), POST only, and an exact path of
// "\n", which no request line can carry, so nothing built from it ever
// admits or matches a real request. Never a real table row.
// Defined once in WebRoutes.cpp so its address is unique across TUs.
extern const RouteSpec ROUTE_NOT_FOUND;

// The table row for id, or nullptr when id is not in routeTable().
const RouteSpec* findRouteSpec(RouteId id);
// The table row for id, or ROUTE_NOT_FOUND (never another route's row) when
// id is not in routeTable(). Callers that can receive an arbitrary id must
// use findRouteSpec() or compare against &ROUTE_NOT_FOUND.
const RouteSpec& routeSpec(RouteId id);
const RouteSpec* routeTable(size_t& count);

// GuardFormBody decision (D23), evaluated at attach time: true when the
// request's declared body (any method, any path) is over WEB_FORM_BODY_MAX
// and must be refused (413) and dropped unread. Exempt: the POST backup
// import (GuardImportBody owns its BACKUP_MAX_BYTES cap) and every /ota/*
// URL (OtaAny drops refused bodies; an admitted upload is a firmware image).
bool formBodyGuardRefuses(bool isPost, const char* url, size_t contentLength);

// Checks, over t[0..n):
//  - Prefix paths end with '/'.
//  - Regular (id >= FirstRegular) rows: POST access != Read; GET access is
//    Read/Public, unless stateChangingGet (then it must be Ota).
//  - Regular rows are unique by (method,path), and no regular Exact route
//    is shadowed by an earlier regular Prefix route with an overlapping
//    method. Guards are excluded from both checks (on either side).
// On failure, `err` (if non-null) receives the offending path (truncated
// to fit cap) and the function returns false.
bool validateRouteTable(const RouteSpec* t, size_t n, char* err, size_t cap);
