#include "WebRoutes.h"
#include <string.h>

namespace {

// Registration order matches WebServerHost::begin (see the plan's "Route
// registration order"): guards, then stage-04 routes, then stage-05 routes.
constexpr RouteSpec ROUTES[] = {
    // -- Guards (Early) ------------------------------------------------
    // GuardFormBody covers every method and path (the "/" prefix matches all
    // URLs); formBodyGuardRefuses() carves out the two body owners below.
    {RouteId::GuardFormBody, RouteMethod::Any, "/", RouteMatch::Prefix, Access::Public, false},
    {RouteId::GuardImportBody, RouteMethod::Post, "/api/backup/import", RouteMatch::Exact, Access::Write, false},
    {RouteId::OtaAny, RouteMethod::Any, "/ota/", RouteMatch::Prefix, Access::Ota, false},
    {RouteId::UpdatePage, RouteMethod::Any, "/update", RouteMatch::Exact, Access::Public, false},
    {RouteId::UpdateSub, RouteMethod::Any, "/update/", RouteMatch::Prefix, Access::Public, false},
    // -- Stage 04 --------------------------------------------------------
    {RouteId::WifiStatus, RouteMethod::Get, "/api/wifi/status", RouteMatch::Exact, Access::Read, false},
    {RouteId::WifiScanGet, RouteMethod::Get, "/api/wifi/scan", RouteMatch::Exact, Access::Read, false},
    {RouteId::WifiScanPost, RouteMethod::Post, "/api/wifi/scan", RouteMatch::Exact, Access::Write, false},
    {RouteId::WifiSave, RouteMethod::Post, "/api/wifi", RouteMatch::Exact, Access::Write, false},
    {RouteId::Version, RouteMethod::Get, "/api/version", RouteMatch::Exact, Access::Public, false},
    {RouteId::SetupPage, RouteMethod::Get, "/setup", RouteMatch::Exact, Access::Public, false},
    // -- Stage 05 ----------------------------------------------------------
    {RouteId::Login, RouteMethod::Post, "/api/login", RouteMatch::Exact, Access::Public, false},
    {RouteId::Logout, RouteMethod::Post, "/api/logout", RouteMatch::Exact, Access::Write, false},
    {RouteId::Session, RouteMethod::Get, "/api/session", RouteMatch::Exact, Access::Read, false},
    {RouteId::State, RouteMethod::Get, "/api/state", RouteMatch::Exact, Access::Read, false},
    {RouteId::Schema, RouteMethod::Get, "/api/schema", RouteMatch::Exact, Access::Read, false},
    {RouteId::ConfigGet, RouteMethod::Get, "/api/config", RouteMatch::Exact, Access::Read, false},
    {RouteId::ConfigSet, RouteMethod::Post, "/api/config", RouteMatch::Exact, Access::Write, false},
    {RouteId::Sensors, RouteMethod::Get, "/api/sensors", RouteMatch::Exact, Access::Read, false},
    {RouteId::SensorAssign, RouteMethod::Post, "/api/sensors/assign", RouteMatch::Exact, Access::Write, false},
    {RouteId::SensorClear, RouteMethod::Post, "/api/sensors/clear", RouteMatch::Exact, Access::Write, false},
    {RouteId::SensorRescan, RouteMethod::Post, "/api/sensors/rescan", RouteMatch::Exact, Access::Write, false},
    {RouteId::Log, RouteMethod::Get, "/api/log", RouteMatch::Exact, Access::Read, false},
    {RouteId::CmdResult, RouteMethod::Get, "/api/cmd", RouteMatch::Exact, Access::Read, false},
    {RouteId::BackupExportReq, RouteMethod::Post, "/api/backup/export", RouteMatch::Exact, Access::Write, false},
    {RouteId::BackupExportGet, RouteMethod::Get, "/api/backup/export", RouteMatch::Exact, Access::Read, false},
    {RouteId::BackupImport, RouteMethod::Post, "/api/backup/import", RouteMatch::Exact, Access::Write, false},
    {RouteId::FactoryReset, RouteMethod::Post, "/api/factory-reset", RouteMatch::Exact, Access::Write, false},
    {RouteId::OtaGrant, RouteMethod::Post, "/api/ota/grant", RouteMatch::Exact, Access::Write, false},
    {RouteId::Root, RouteMethod::Get, "/", RouteMatch::Exact, Access::Public, false},
};

constexpr size_t ROUTE_COUNT = sizeof(ROUTES) / sizeof(ROUTES[0]);

bool isGuard(const RouteSpec& r) {
    return static_cast<uint8_t>(r.id) < static_cast<uint8_t>(FirstRegular);
}

bool methodOverlaps(RouteMethod a, RouteMethod b) {
    return a == b || a == RouteMethod::Any || b == RouteMethod::Any;
}

void setErr(char* err, size_t cap, const char* path) {
    if (err == nullptr || cap == 0) {
        return;
    }
    size_t len = strlen(path);
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(err, path, len);
    err[len] = '\0';
}

}  // namespace

const RouteSpec* routeTable(size_t& count) {
    count = ROUTE_COUNT;
    return ROUTES;
}

const RouteSpec ROUTE_NOT_FOUND = {
    RouteId::Static, RouteMethod::Post, "\n", RouteMatch::Exact, Access::Ota, false};

const RouteSpec* findRouteSpec(RouteId id) {
    for (size_t i = 0; i < ROUTE_COUNT; ++i) {
        if (ROUTES[i].id == id) {
            return &ROUTES[i];
        }
    }
    return nullptr;
}

const RouteSpec& routeSpec(RouteId id) {
    const RouteSpec* spec = findRouteSpec(id);
    return spec != nullptr ? *spec : ROUTE_NOT_FOUND;
}

bool formBodyGuardRefuses(bool isPost, const char* url, size_t contentLength) {
    if (url == nullptr || contentLength <= WEB_FORM_BODY_MAX) {
        return false;
    }
    if (isPost && strcmp(url, routeSpec(RouteId::GuardImportBody).path) == 0) {
        return false;   // GuardImportBody: own cap (BACKUP_MAX_BYTES), after the gate
    }
    const char* ota = routeSpec(RouteId::OtaAny).path;
    if (strncmp(url, ota, strlen(ota)) == 0) {
        return false;   // OtaAny: refused bodies dropped there; an admitted upload is an image
    }
    return true;
}

bool validateRouteTable(const RouteSpec* t, size_t n, char* err, size_t cap) {
    for (size_t i = 0; i < n; ++i) {
        const RouteSpec& r = t[i];

        if (r.match == RouteMatch::Prefix) {
            size_t len = strlen(r.path);
            if (len == 0 || r.path[len - 1] != '/') {
                setErr(err, cap, r.path);
                return false;
            }
        }

        if (isGuard(r)) {
            continue;   // exempt from method/access rules and from uniqueness/shadow checks
        }

        if (r.method == RouteMethod::Post && r.access == Access::Read) {
            setErr(err, cap, r.path);
            return false;
        }
        if (r.method == RouteMethod::Get) {
            if (r.stateChangingGet) {
                if (r.access != Access::Ota) {
                    setErr(err, cap, r.path);
                    return false;
                }
            } else if (r.access != Access::Read && r.access != Access::Public) {
                setErr(err, cap, r.path);
                return false;
            }
        }

        for (size_t k = 0; k < i; ++k) {
            const RouteSpec& p = t[k];
            if (isGuard(p) || !methodOverlaps(p.method, r.method)) {
                continue;
            }
            if (strcmp(p.path, r.path) == 0) {
                setErr(err, cap, r.path);
                return false;
            }
            if (r.match == RouteMatch::Exact && p.match == RouteMatch::Prefix) {
                size_t plen = strlen(p.path);
                if (strncmp(r.path, p.path, plen) == 0) {
                    setErr(err, cap, r.path);
                    return false;
                }
            }
        }
    }
    return true;
}
