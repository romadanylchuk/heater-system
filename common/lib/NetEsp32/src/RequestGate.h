#pragma once
#include <ESPAsyncWebServer.h>
#include <WebAccess.h>
#include <WebRoutes.h>

// Access gate for every protected web route (stage 05, D2). Owned by
// NetEsp32 as an interface so NetEsp32 does not depend on WebEsp32: the
// session/CSRF/OTA-grant implementation (SessionGate) lives in WebEsp32 and
// is injected through ConnectivityServices::setWebLayer(). A null gate fails
// closed (WebServerHost answers 401 for every non-Public route).
class RequestGate {
public:
    virtual ~RequestGate() = default;
    // AsyncTCP task. Public always Ok. Read: valid session cookie. Write: +
    // X-CSRF-Token. Ota: + an unexpired OTA grant of that session.
    virtual GateResult check(AsyncWebServerRequest* r, Access level) const = 0;
};

// 401 {"ok":false,"error":"auth"} | 403 {"ok":false,"error":"csrf"} |
// 403 {"ok":false,"error":"ota_grant"}. Ok (a caller bug) answers 500.
void sendGateDenied(AsyncWebServerRequest* r, GateResult res);

// AsyncURIMatcher::exact / ::prefix built from the route table entry (D19):
// the only way routes are registered (no string literals in server.on).
AsyncURIMatcher routeMatcher(RouteId id);
WebRequestMethodComposite routeMethod(RouteId id);

// Attach-time refusal handler that drops the request body unread (stage 05
// D23/D9; review-5 Must-fix). An AsyncCallbackWebHandler cannot be used for
// a refusal: with a request callback isRequestHandlerTrivial() is false, so
// ESPAsyncWebServer 3.12.1 parses an urlencoded body (_parsePlainPostChar,
// an unbounded String per param) and multipart fields/part headers into
// heap; without one it never attaches (canHandle is false). This handler
// keeps the base class's isRequestHandlerTrivial() == true, so the library
// only counts the body bytes (the no-op base handleBody), never buffers
// them; `respond` runs once the discarded body has been received. The
// refusal decision is the handler's filter (setFilter), evaluated once at
// attach time. Register it with server.addHandler() (which owns it).
// canHandle ignores the connection type the library inferred (isHTTP()):
// a WebSocket/SSE-tagged request (RCT_WS/RCT_EVENT) is claimed like any
// other, so a header trick cannot route a body past a guard (review-5
// iteration 1). The (matcher, method) overload serves the final catch-all
// (WebApiRoutes.cpp), which replaces the form-parsing onNotFound handler.
class BodyGuardHandler : public AsyncWebHandler {
public:
    BodyGuardHandler(RouteId id, ArRequestHandlerFunction respond);
    BodyGuardHandler(AsyncURIMatcher uri, WebRequestMethodComposite method, ArRequestHandlerFunction respond);

    bool canHandle(AsyncWebServerRequest* r) const override;
    void handleRequest(AsyncWebServerRequest* r) override;

private:
    AsyncURIMatcher _uri;
    WebRequestMethodComposite _method;
    ArRequestHandlerFunction _respond;
};
