// Read routes, login and static serving of WebServices (stage 05). Every
// handler runs on the AsyncTCP task: it checks the gate at the route table's
// access level first and then only touches SharedJsonBuffers, the
// SessionGate, the CommandResultBoard (under _mux) or immutable data (the
// constexpr schema tables). No CommonState/ConfigEngine access here.
#include <Arduino.h>
#include <LittleFS.h>
#include <memory>
#include <string.h>
#include <CookieParse.h>
#include <JsonOut.h>
#include <RequestGate.h>
#include <SchemaJsonStream.h>
#include <WebInput.h>
#include <WebJson.h>
#include <WebRoutes.h>
#include "WebHttp.h"
#include "WebServices.h"

namespace {

using webhttp::admit;
using webhttp::formParam;
using webhttp::JSON_TYPE;
using webhttp::sendJson;

constexpr size_t COOKIE_TEXT_MAX = 128;

}  // namespace

void WebServices::installRead(AsyncWebServer& s) {
    // POST /api/login (Public): form user/pass.
    s.on(routeMatcher(RouteId::Login), routeMethod(RouteId::Login), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::Login)) {
            return;
        }
        const String* user = formParam(r, "user");
        const String* pass = formParam(r, "pass");
        if (user == nullptr || pass == nullptr || user->length() == 0 || pass->length() == 0) {
            sendJson(r, 400, "{\"ok\":false,\"error\":\"invalid\"}");
            return;
        }
        SessionTokens tok;
        uint32_t retryS = 0;
        SessionGate::LoginResult res =
            _gate.login(user->c_str(), user->length(), pass->c_str(), pass->length(), tok, retryS);
        if (res == SessionGate::LoginResult::Throttled) {
            char body[64];
            snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"throttled\",\"retryS\":%lu}",
                static_cast<unsigned long>(retryS));
            sendJson(r, 429, body);
            return;
        }
        if (res != SessionGate::LoginResult::Ok) {
            sendJson(r, 401, "{\"ok\":false,\"error\":\"credentials\"}");
            return;
        }
        char body[192];
        JsonOut j(body, sizeof(body));
        j.beginObject();
        j.key("ok");
        j.boolean(true);
        j.key("csrf");
        j.str(tok.csrf);
        j.key("user");
        j.str(user->c_str());  // verified == the stored user (<= USER_MAX)
        j.key("ttlS");
        j.integer(SESSION_TTL_S);
        j.endObject();
        char cookie[COOKIE_TEXT_MAX];
        if (!j.ok() || formatSessionCookie(tok.sid, SESSION_TTL_S, cookie, sizeof(cookie)) == 0) {
            sendJson(r, 500, "{\"ok\":false,\"error\":\"internal\"}");
            return;
        }
        AsyncWebServerResponse* resp = r->beginResponse(200, JSON_TYPE, body);
        resp->addHeader("Cache-Control", "no-store");
        resp->addHeader("Set-Cookie", cookie);
        r->send(resp);
    });

    // GET /api/session (Read): the CSRF token for a reloaded page.
    s.on(routeMatcher(RouteId::Session), routeMethod(RouteId::Session), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::Session)) {
            return;
        }
        char csrf[TOKEN_HEX_LEN + 1];
        uint32_t ttlS = 0;
        bool ota = false;
        if (!_gate.sessionInfo(r, csrf, sizeof(csrf), ttlS, ota)) {
            sendGateDenied(r, GateResult::NoSession);  // expired between the two lookups
            return;
        }
        char body[128];
        snprintf(body, sizeof(body), "{\"ok\":true,\"csrf\":\"%s\",\"ttlS\":%lu,\"ota\":%s}", csrf,
            static_cast<unsigned long>(ttlS), ota ? "true" : "false");
        sendJson(r, 200, body);
    });

    s.on(routeMatcher(RouteId::State), routeMethod(RouteId::State), [this](AsyncWebServerRequest* r) {
        if (admit(_gate, r, RouteId::State)) {
            _stateJson.send(r);
        }
    });
    s.on(routeMatcher(RouteId::Sensors), routeMethod(RouteId::Sensors), [this](AsyncWebServerRequest* r) {
        if (admit(_gate, r, RouteId::Sensors)) {
            _sensorsJson.send(r);
        }
    });
    s.on(routeMatcher(RouteId::ConfigGet), routeMethod(RouteId::ConfigGet), [this](AsyncWebServerRequest* r) {
        if (admit(_gate, r, RouteId::ConfigGet)) {
            _configJson.send(r);
        }
    });
    s.on(routeMatcher(RouteId::Log), routeMethod(RouteId::Log), [this](AsyncWebServerRequest* r) {
        if (admit(_gate, r, RouteId::Log)) {
            _logJson.send(r);
        }
    });

    // GET /api/schema (Read): streamed from the constexpr tables, no lock.
    s.on(routeMatcher(RouteId::Schema), routeMethod(RouteId::Schema), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::Schema)) {
            return;
        }
        auto stream = std::make_shared<SchemaJsonStream>(_schema, _schema.controllerType);
        AsyncWebServerResponse* resp = r->beginChunkedResponse(JSON_TYPE,
            [stream](uint8_t* buf, size_t maxLen, size_t) -> size_t {
                return stream->read(reinterpret_cast<char*>(buf), maxLen);
            });
        resp->addHeader("Cache-Control", "no-cache");
        r->send(resp);
    });

    // GET /api/cmd?id=N (Read): the result board entry of a web command.
    s.on(routeMatcher(RouteId::CmdResult), routeMethod(RouteId::CmdResult), [this](AsyncWebServerRequest* r) {
        if (!admit(_gate, r, RouteId::CmdResult)) {
            return;
        }
        const AsyncWebParameter* p = r->getParam("id");
        uint32_t id = 0;
        if (p == nullptr || !parseU32(p->value().c_str(), id)) {
            sendJson(r, 400, "{\"ok\":false,\"error\":\"invalid\"}");
            return;
        }
        CmdResult res;
        portENTER_CRITICAL(&_mux);
        CmdLookup l = _board.lookup(id, res);
        portEXIT_CRITICAL(&_mux);
        if (l == CmdLookup::Unknown) {
            sendJson(r, 404, "{\"ok\":false,\"error\":\"not_found\"}");
            return;
        }
        char body[128];
        if (buildCmdResultJson(l, res, id, body, sizeof(body)) == 0) {
            sendJson(r, 500, "{\"ok\":false,\"error\":\"internal\"}");
            return;
        }
        sendJson(r, 200, body);
    });
}

void WebServices::installStatic(AsyncWebServer& s) {
    // GET / (Public): the SPA shell, or the PROGMEM rescue page when the
    // LittleFS image is missing or a filesystem OTA is rewriting it.
    s.on(routeMatcher(RouteId::Root), routeMethod(RouteId::Root), [this](AsyncWebServerRequest* r) {
        if (!_webImage.load() || otaBusyNow()) {
            sendRescuePage(r);
            return;
        }
        // AsyncFileResponse picks "/index.html.gz" (Content-Encoding: gzip).
        AsyncWebServerResponse* resp = r->beginResponse(LittleFS, "/index.html", "text/html");
        resp->addHeader("Cache-Control", "no-cache");
        r->send(resp);
    });

    // Every other static file (gzip variants picked by the library), never
    // while an OTA is in progress (a filesystem OTA rewrites the partition).
    s.serveStatic("/", LittleFS, "/")
        .setCacheControl("no-cache")
        .setFilter([this](AsyncWebServerRequest*) { return _webImage.load() && !otaBusyNow(); });

    // Catch-all, registered last (installStatic is the final Late step and
    // Late the final installer). Not onNotFound: the library attaches its
    // _catchAllHandler without canHandle, and a catch-all with a request
    // callback is non-trivial, so it form-parses (unbounded String) any body
    // an unmatched request carries - e.g. one tagged WebSocket/SSE (review-5
    // iteration 1 Must-fix). A BodyGuardHandler matching every method and
    // target is trivial: the body is only counted, never buffered. The
    // library's own catch-all then stays callback-less (trivial, plain 404)
    // and is unreachable. The callback below reads no body parameters.
    s.addHandler(new BodyGuardHandler(AsyncURIMatcher::all(), AsyncWebRequestMethod::HTTP_ALL, [this](AsyncWebServerRequest* r) {
        // Captive portal: any foreign host while the setup AP is up goes to
        // the AP's own address. apIp() is complete whenever active() is true.
        // Only for requests that arrived on the AP interface: the AP runs in
        // AP+STA mode, and a LAN client reaching the STA address must get a
        // normal 404, not a redirect to an address it cannot reach
        // (review-4 Should-fix 2).
        if (_captive.active()) {
            const char* apIp = _captive.apIp();
            IPAddress apAddr;
            AsyncClient* client = r->client();
            bool onAp = client != nullptr && apAddr.fromString(apIp) && client->localIP() == apAddr;
            const String& host = r->host();
            if (onAp && !host.equals(apIp)) {
                char location[32];
                snprintf(location, sizeof(location), "http://%s/", apIp);
                r->redirect(location);
                return;
            }
        }
        if (r->url().startsWith("/api/")) {
            r->send(404, JSON_TYPE, "{\"ok\":false,\"error\":\"not_found\"}");
            return;
        }
        r->send(404, "text/plain", "Not found");
    }));
}
