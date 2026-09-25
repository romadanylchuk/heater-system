#include "RequestGate.h"
#include <string.h>
#include <utility>

namespace {

constexpr const char* JSON_TYPE = "application/json";

}  // namespace

void sendGateDenied(AsyncWebServerRequest* r, GateResult res) {
    if (r == nullptr) {
        return;
    }
    switch (res) {
        case GateResult::NoSession:
            r->send(401, JSON_TYPE, "{\"ok\":false,\"error\":\"auth\"}");
            break;
        case GateResult::BadCsrf:
            r->send(403, JSON_TYPE, "{\"ok\":false,\"error\":\"csrf\"}");
            break;
        case GateResult::NoOtaGrant:
            r->send(403, JSON_TYPE, "{\"ok\":false,\"error\":\"ota_grant\"}");
            break;
        case GateResult::Ok:
        default:
            r->send(500, JSON_TYPE, "{\"ok\":false,\"error\":\"internal\"}");
            break;
    }
}

AsyncURIMatcher routeMatcher(RouteId id) {
    // An id outside the table yields ROUTE_NOT_FOUND: an exact "\n" path
    // that no request matches, never another route's matcher.
    const RouteSpec& spec = routeSpec(id);
    if (spec.match == RouteMatch::Prefix && strcmp(spec.path, "/") == 0) {
        // The root prefix means every request target, including a
        // non-origin-form one ("*", absolute-form) that the library keeps
        // verbatim in url() and a "/" startsWith would miss (GuardFormBody).
        return AsyncURIMatcher::all();
    }
    return spec.match == RouteMatch::Prefix ? AsyncURIMatcher::prefix(spec.path) : AsyncURIMatcher::exact(spec.path);
}

WebRequestMethodComposite routeMethod(RouteId id) {
    switch (routeSpec(id).method) {
        case RouteMethod::Get:
            return AsyncWebRequestMethod::HTTP_GET;
        case RouteMethod::Post:
            return AsyncWebRequestMethod::HTTP_POST;
        case RouteMethod::Any:
        default:
            return AsyncWebRequestMethod::HTTP_ALL;
    }
}

BodyGuardHandler::BodyGuardHandler(RouteId id, ArRequestHandlerFunction respond)
    : BodyGuardHandler(routeMatcher(id), routeMethod(id), std::move(respond)) {}

BodyGuardHandler::BodyGuardHandler(AsyncURIMatcher uri, WebRequestMethodComposite method,
    ArRequestHandlerFunction respond)
    : _uri(std::move(uri)), _method(method), _respond(std::move(respond)) {}

bool BodyGuardHandler::canHandle(AsyncWebServerRequest* r) const {
    // Deliberately no isHTTP(): the library tags a GET carrying
    // "Upgrade: websocket" / "Accept: text/event-stream" as RCT_WS/RCT_EVENT,
    // and a guard that skipped those would let their body fall through to a
    // form-parsing handler (review-5 iteration 1 Must-fix). The project serves
    // no WebSocket or SSE endpoint, so claiming them costs nothing.
    return _method.matches(r->method()) && _uri.matches(r);
}

void BodyGuardHandler::handleRequest(AsyncWebServerRequest* r) {
    if (_respond) {
        _respond(r);
    }
}
