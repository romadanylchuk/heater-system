#pragma once
#include <ESPAsyncWebServer.h>
#include <RequestGate.h>
#include <WebRoutes.h>
#include "SessionGate.h"

// Small HTTP helpers shared by the WebServices route files (WebApiRoutes.cpp,
// WebApiWrite.cpp). AsyncTCP task only; no CommonState access.
namespace webhttp {

constexpr const char* JSON_TYPE = "application/json";

// Runs the gate at the route table's access level; answers the denial itself.
inline bool admit(const SessionGate& gate, AsyncWebServerRequest* r, RouteId id) {
    const RouteSpec* spec = findRouteSpec(id);
    if (spec == nullptr) {
        // Not a table route (documentation-only id): never admit.
        r->send(500, JSON_TYPE, "{\"ok\":false,\"error\":\"internal\"}");
        return false;
    }
    GateResult res = gate.check(r, spec->access);
    if (res == GateResult::Ok) {
        return true;
    }
    sendGateDenied(r, res);
    return false;
}

// Form (body) parameter, or nullptr when absent.
inline const String* formParam(AsyncWebServerRequest* r, const char* name) {
    const AsyncWebParameter* p = r->getParam(name, true);
    return p != nullptr ? &p->value() : nullptr;
}

// The body is copied into the response (AsyncBasicResponse), so a stack
// buffer is fine.
inline void sendJson(AsyncWebServerRequest* r, int code, const char* json) {
    AsyncWebServerResponse* resp = r->beginResponse(code, JSON_TYPE, json);
    resp->addHeader("Cache-Control", "no-store");
    r->send(resp);
}

}  // namespace webhttp
