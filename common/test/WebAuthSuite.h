#pragma once
#include <unity.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/WebEngine/src/CommandResultBoard.h"
#include "../lib/WebEngine/src/CookieParse.h"
#include "../lib/WebEngine/src/ExportSlot.h"
#include "../lib/WebEngine/src/JsonOut.h"
#include "../lib/WebEngine/src/LoginThrottle.h"
#include "../lib/WebEngine/src/SessionStore.h"
#include "../lib/WebEngine/src/WebRoutes.h"

// Native-safe Unity tests for stage 05 phase 1: the pure WebEngine
// primitives (JsonOut, SessionStore/CookieParse, LoginThrottle,
// CommandResultBoard, ExportSlot, WebRoutes). Header-only, run via
// WebSuite.h. Test names are prefixed web_auth_.

// ---- a counter-based fake RNG for token-uniqueness tests -------------------

namespace {
uint32_t g_webAuthRandomCounter = 0;

void webAuthFillRandom(uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>((g_webAuthRandomCounter + i * 7 + 1) & 0xFF);
    }
    ++g_webAuthRandomCounter;
}
}  // namespace

// ---- JsonOut -----------------------------------------------------------

static void web_auth_test_jsonout_escaping() {
    char buf[128];
    JsonOut j(buf, sizeof(buf));
    j.beginObject();
    j.key("s");
    j.str("a\"b\\c\001d");   // octal escape: exactly one 0x01 char, then 'd' (unlike \x01d, which
                             // C++ would parse as the single hex escape \x01d)
    j.endObject();
    TEST_ASSERT_TRUE(j.ok());
    TEST_ASSERT_EQUAL_STRING("{\"s\":\"a\\\"b\\\\c\\u0001d\"}", buf);
}

static void web_auth_test_jsonout_nesting_commas() {
    char buf[128];
    JsonOut j(buf, sizeof(buf));
    j.beginObject();
    j.key("a");
    j.integer(1);
    j.key("arr");
    j.beginArray();
    j.integer(1);
    j.integer(2);
    j.str("x");
    j.endArray();
    j.key("b");
    j.boolean(true);
    j.endObject();
    TEST_ASSERT_TRUE(j.ok());
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"arr\":[1,2,\"x\"],\"b\":true}", buf);
}

static void web_auth_test_jsonout_overflow_latch() {
    char buf[8];
    JsonOut j(buf, sizeof(buf));
    j.beginObject();
    j.key("longkey");
    j.str("value");
    j.endObject();
    TEST_ASSERT_FALSE(j.ok());
    TEST_ASSERT_EQUAL_UINT(0, j.length());
    // The buffer must still be NUL-terminated even after overflow.
    TEST_ASSERT_TRUE(strnlen(buf, sizeof(buf)) < sizeof(buf));
}

static void web_auth_test_jsonout_nan_to_null() {
    char buf[64];
    JsonOut j(buf, sizeof(buf));
    j.beginArray();
    j.num(0.0f / 0.0f, 2);   // NaN
    j.num(1.0f / 0.0f, 2);   // +inf
    j.num(2.5f, 2);
    j.num(3.0f, 2);
    j.endArray();
    TEST_ASSERT_TRUE(j.ok());
    TEST_ASSERT_EQUAL_STRING("[null,null,2.5,3]", buf);
}

// ---- SessionStore -------------------------------------------------------

static void web_auth_test_session_create_authorize_levels() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(1000, 1, tok));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
        static_cast<int>(store.authorize(tok.sid, nullptr, Access::Read, 1001, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
        static_cast<int>(store.authorize(tok.sid, tok.csrf, Access::Write, 1001, 1)));
    // Ota without a grant yet.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoOtaGrant),
        static_cast<int>(store.authorize(tok.sid, tok.csrf, Access::Ota, 1001, 1)));
}

static void web_auth_test_session_bad_csrf() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(1000, 1, tok));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::BadCsrf),
        static_cast<int>(store.authorize(tok.sid, "wrongtoken0000000000000000000000", Access::Write, 1001, 1)));
}

static void web_auth_test_session_ota_grant_expires() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(1000, 1, tok));
    TEST_ASSERT_TRUE(store.grantOta(tok.sid, 1000, 1));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
        static_cast<int>(store.authorize(tok.sid, tok.csrf, Access::Ota, 1000 + OTA_GRANT_TTL_MS - 1, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoOtaGrant),
        static_cast<int>(store.authorize(tok.sid, tok.csrf, Access::Ota, 1000 + OTA_GRANT_TTL_MS, 1)));
}

static void web_auth_test_session_24h_expiry() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(0, 1, tok));

    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
        static_cast<int>(store.authorize(tok.sid, nullptr, Access::Read, SESSION_TTL_MS - 1, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoSession),
        static_cast<int>(store.authorize(tok.sid, nullptr, Access::Read, SESSION_TTL_MS, 1)));
}

static void web_auth_test_session_epoch_change_invalidates() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(1000, 1, tok));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoSession),
        static_cast<int>(store.authorize(tok.sid, nullptr, Access::Read, 1001, 2)));
}

static void web_auth_test_session_evicts_oldest_when_full() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens toks[5];
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(store.create(static_cast<uint64_t>(i), 1, toks[i]));
    }
    TEST_ASSERT_TRUE(store.create(4, 1, toks[4]));

    // The oldest (index 0) was evicted; the rest remain valid.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoSession),
        static_cast<int>(store.authorize(toks[0].sid, nullptr, Access::Read, 5, 1)));
    for (int i = 1; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
            static_cast<int>(store.authorize(toks[i].sid, nullptr, Access::Read, 5, 1)));
    }
}

static void web_auth_test_session_revoke_and_revoke_all() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens a, b;
    TEST_ASSERT_TRUE(store.create(1000, 1, a));
    TEST_ASSERT_TRUE(store.create(1001, 1, b));

    store.revoke(a.sid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoSession),
        static_cast<int>(store.authorize(a.sid, nullptr, Access::Read, 1002, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
        static_cast<int>(store.authorize(b.sid, nullptr, Access::Read, 1002, 1)));

    store.revokeAll();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoSession),
        static_cast<int>(store.authorize(b.sid, nullptr, Access::Read, 1002, 1)));
}

static void web_auth_test_session_token_uniqueness() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens a, b, c;
    TEST_ASSERT_TRUE(store.create(0, 1, a));
    TEST_ASSERT_TRUE(store.create(1, 1, b));
    TEST_ASSERT_TRUE(store.create(2, 1, c));

    TEST_ASSERT_NOT_EQUAL(0, strcmp(a.sid, b.sid));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(b.sid, c.sid));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a.csrf, b.csrf));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a.sid, a.csrf));
}

static void web_auth_test_session_create_fails_without_rng() {
    SessionStore store(nullptr);
    SessionTokens tok;
    TEST_ASSERT_FALSE(store.create(0, 1, tok));
}

static void web_auth_test_session_csrf_for_and_remaining_s() {
    SessionStore store(&webAuthFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(0, 1, tok));

    char csrf[TOKEN_HEX_LEN + 1] = {};
    TEST_ASSERT_TRUE(store.csrfFor(tok.sid, 1000, 1, csrf, sizeof(csrf)));
    TEST_ASSERT_EQUAL_STRING(tok.csrf, csrf);

    uint32_t remain = store.remainingS(tok.sid, 1000, 1);
    TEST_ASSERT_EQUAL_UINT32(SESSION_TTL_S - 1, remain);

    // Unknown sid.
    TEST_ASSERT_EQUAL_UINT32(0, store.remainingS("deadbeef", 1000, 1));
}

// ---- CookieParse ---------------------------------------------------------

static void web_auth_test_cookie_parse_among_several() {
    char out[64];
    TEST_ASSERT_TRUE(findCookie("a=1; hsid=abc123; b=2", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc123", out);
}

static void web_auth_test_cookie_prefix_name_trap() {
    char out[64];
    TEST_ASSERT_TRUE(findCookie("xhsid=1; hsid=2", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("2", out);
}

static void web_auth_test_cookie_overflow() {
    char out[3];
    TEST_ASSERT_FALSE(findCookie("hsid=abcdef", "hsid", out, sizeof(out)));
}

// Malformed headers (final-check Should 2): a segment with no '=' is
// skipped, empty segments are skipped, an empty value is an empty string.
static void web_auth_test_cookie_malformed_segments() {
    char out[64];
    TEST_ASSERT_TRUE(findCookie("foo;hsid=abc", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_TRUE(findCookie(";;hsid=abc", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_TRUE(findCookie("a=1;;hsid=abc", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_TRUE(findCookie("=x; hsid=abc;", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    // A bare name with no '=' is not a cookie with that name.
    TEST_ASSERT_FALSE(findCookie("hsid", "hsid", out, sizeof(out)));
    TEST_ASSERT_FALSE(findCookie("hsid; a=1", "hsid", out, sizeof(out)));
    TEST_ASSERT_FALSE(findCookie("", "hsid", out, sizeof(out)));
    TEST_ASSERT_FALSE(findCookie(";;;", "hsid", out, sizeof(out)));
    // Empty value: found, empty string (no session matches it).
    out[0] = 'x';
    TEST_ASSERT_TRUE(findCookie("hsid=", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_TRUE(findCookie("a=1; hsid=; b=2", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    // Duplicate name: the first one wins.
    TEST_ASSERT_TRUE(findCookie("hsid=first; hsid=second", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("first", out);
}

static void web_auth_test_cookie_whitespace() {
    char out[64];
    // Leading spaces/tabs before a name and trailing ones after a value are dropped.
    TEST_ASSERT_TRUE(findCookie(" \t hsid=abc \t ; b=2", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_TRUE(findCookie("a=1;\thsid=abc", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    // A space inside the name is part of the name: no match.
    TEST_ASSERT_FALSE(findCookie("hsid =abc", "hsid", out, sizeof(out)));
    // Whitespace-only value trims to empty.
    TEST_ASSERT_TRUE(findCookie("hsid=   ", "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void web_auth_test_cookie_very_long_header() {
    static char header[4096 + 32];
    size_t pos = 0;
    while (pos + 8 < 4096) {
        memcpy(header + pos, "zz=yyy; ", 8);
        pos += 8;
    }
    memcpy(header + pos, "hsid=abc", 9);   // includes the terminator
    char out[64];
    TEST_ASSERT_TRUE(findCookie(header, "hsid", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);

    // A very long value never overruns `out`: refused, not truncated.
    memcpy(header, "hsid=", 5);
    memset(header + 5, 'v', 4000);
    header[4005] = '\0';
    TEST_ASSERT_FALSE(findCookie(header, "hsid", out, sizeof(out)));

    // Very long header without the cookie.
    memset(header, ';', 4000);
    header[4000] = '\0';
    TEST_ASSERT_FALSE(findCookie(header, "hsid", out, sizeof(out)));
}

static void web_auth_test_cookie_format_session_cookie() {
    char out[96];
    size_t n = formatSessionCookie("abc123", 86400, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("hsid=abc123; Path=/; Max-Age=86400; HttpOnly; SameSite=Strict", out);

    n = formatSessionCookie("abc123", 0, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("hsid=abc123; Path=/; Max-Age=0; HttpOnly; SameSite=Strict", out);
}

// ---- LoginThrottle ---------------------------------------------------------

static void web_auth_test_throttle_locks_after_fifth_failure() {
    LoginThrottle t;
    uint32_t retryS = 0;
    for (int i = 0; i < 4; ++i) {
        t.onFailure(1000);
        TEST_ASSERT_TRUE(t.allowed(1000, retryS));
    }
    t.onFailure(1000);   // 5th failure locks
    TEST_ASSERT_FALSE(t.allowed(1000, retryS));
    TEST_ASSERT_EQUAL_UINT32(30, retryS);

    TEST_ASSERT_FALSE(t.allowed(1000 + LoginThrottle::LOCK_MS - 1, retryS));
    TEST_ASSERT_TRUE(t.allowed(1000 + LoginThrottle::LOCK_MS, retryS));
}

static void web_auth_test_throttle_success_resets() {
    LoginThrottle t;
    uint32_t retryS = 0;
    for (int i = 0; i < 4; ++i) {
        t.onFailure(1000);
    }
    t.onSuccess();
    for (int i = 0; i < 4; ++i) {
        t.onFailure(2000);
        TEST_ASSERT_TRUE(t.allowed(2000, retryS));
    }
}

// ---- CommandResultBoard ---------------------------------------------------

static void web_auth_test_board_pending_done_unknown() {
    CommandResultBoard board;
    CmdResult r;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Unknown), static_cast<int>(board.lookup(1, r)));

    board.expect(1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Pending), static_cast<int>(board.lookup(1, r)));

    board.record(1, CommandStatus::Clamped, true, 55.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Done), static_cast<int>(board.lookup(1, r)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandStatus::Clamped), static_cast<int>(r.status));
    TEST_ASSERT_TRUE(r.hasValue);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, r.value);
}

static void web_auth_test_board_evicts_after_capacity() {
    CommandResultBoard board;
    for (uint32_t i = 0; i < CommandResultBoard::CAPACITY; ++i) {
        board.expect(i + 1);
    }
    CmdResult r;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Pending), static_cast<int>(board.lookup(1, r)));

    board.expect(CommandResultBoard::CAPACITY + 1);   // overwrites the oldest (id 1)
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Unknown), static_cast<int>(board.lookup(1, r)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Pending),
        static_cast<int>(board.lookup(CommandResultBoard::CAPACITY + 1, r)));
}

static void web_auth_test_board_record_unexpected_ignored() {
    CommandResultBoard board;
    board.record(99, CommandStatus::Ok, false, 0.0f);
    CmdResult r;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdLookup::Unknown), static_cast<int>(board.lookup(99, r)));
}

// ---- ExportSlot ------------------------------------------------------------

static void web_auth_test_export_slot_full_lifecycle() {
    ExportSlot slot;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Idle), static_cast<int>(slot.state()));

    TEST_ASSERT_TRUE(slot.request());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Requested), static_cast<int>(slot.state()));
    TEST_ASSERT_TRUE(slot.needsBuild());

    // A repeated request() while Requested/Ready is a harmless no-op.
    TEST_ASSERT_TRUE(slot.request());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Requested), static_cast<int>(slot.state()));

    char* data = static_cast<char*>(malloc(4));
    memcpy(data, "abcd", 4);
    slot.publish(data, 4, 1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Ready), static_cast<int>(slot.state()));
    TEST_ASSERT_FALSE(slot.needsBuild());

    char* taken = nullptr;
    size_t len = 0;
    TEST_ASSERT_TRUE(slot.take(taken, len));
    TEST_ASSERT_EQUAL_UINT(4, len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(taken, "abcd", 4));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Idle), static_cast<int>(slot.state()));
    free(taken);

    TEST_ASSERT_FALSE(slot.take(taken, len));   // nothing left to take
}

static void web_auth_test_export_slot_build_failure_goes_idle() {
    ExportSlot slot;
    TEST_ASSERT_TRUE(slot.request());
    slot.publish(nullptr, 0, 1000);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Idle), static_cast<int>(slot.state()));
}

static void web_auth_test_export_slot_ttl_expiry_returns_buffer() {
    ExportSlot slot;
    TEST_ASSERT_TRUE(slot.request());
    char* data = static_cast<char*>(malloc(2));
    memcpy(data, "hi", 2);
    slot.publish(data, 2, 1000);

    TEST_ASSERT_NULL(slot.expire(1000 + ExportSlot::READY_TTL_MS - 1));
    char* expired = slot.expire(1000 + ExportSlot::READY_TTL_MS);
    TEST_ASSERT_NOT_NULL(expired);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ExportState::Idle), static_cast<int>(slot.state()));
    free(expired);
}

// ---- WebRoutes -------------------------------------------------------------

static void web_auth_test_routes_real_table_validates() {
    size_t n = 0;
    const RouteSpec* t = routeTable(n);
    char err[64] = {};
    TEST_ASSERT_TRUE(validateRouteTable(t, n, err, sizeof(err)));
}

static void web_auth_test_routes_duplicate_fails() {
    RouteSpec bad[] = {
        {RouteId::WifiStatus, RouteMethod::Get, "/api/x", RouteMatch::Exact, Access::Read, false},
        {RouteId::Version, RouteMethod::Get, "/api/x", RouteMatch::Exact, Access::Public, false},
    };
    char err[64] = {};
    TEST_ASSERT_FALSE(validateRouteTable(bad, 2, err, sizeof(err)));
}

static void web_auth_test_routes_prefix_without_trailing_slash_fails() {
    RouteSpec bad[] = {
        {RouteId::WifiStatus, RouteMethod::Get, "/api", RouteMatch::Prefix, Access::Read, false},
    };
    char err[64] = {};
    TEST_ASSERT_FALSE(validateRouteTable(bad, 1, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("/api", err);
}

static void web_auth_test_routes_post_with_read_fails() {
    RouteSpec bad[] = {
        {RouteId::WifiSave, RouteMethod::Post, "/api/wifi", RouteMatch::Exact, Access::Read, false},
    };
    char err[64] = {};
    TEST_ASSERT_FALSE(validateRouteTable(bad, 1, err, sizeof(err)));
}

static void web_auth_test_routes_get_with_write_fails() {
    RouteSpec bad[] = {
        {RouteId::WifiStatus, RouteMethod::Get, "/api/wifi/status", RouteMatch::Exact, Access::Write, false},
    };
    char err[64] = {};
    TEST_ASSERT_FALSE(validateRouteTable(bad, 1, err, sizeof(err)));
}

static void web_auth_test_routes_shadowed_exact_fails() {
    RouteSpec bad[] = {
        {RouteId::WifiStatus, RouteMethod::Get, "/api/", RouteMatch::Prefix, Access::Read, false},
        {RouteId::Version, RouteMethod::Get, "/api/version", RouteMatch::Exact, Access::Public, false},
    };
    char err[64] = {};
    TEST_ASSERT_FALSE(validateRouteTable(bad, 2, err, sizeof(err)));
}

// GuardFormBody is global (review-5 Must-fix): any method, any path. As a
// guard its "/" prefix must not count as shadowing the regular routes.
static void web_auth_test_routes_form_guard_is_global() {
    const RouteSpec& g = routeSpec(RouteId::GuardFormBody);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteMethod::Any), static_cast<int>(g.method));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteMatch::Prefix), static_cast<int>(g.match));
    TEST_ASSERT_EQUAL_STRING("/", g.path);
    RouteSpec t[] = {
        g,
        {RouteId::Version, RouteMethod::Get, "/api/version", RouteMatch::Exact, Access::Public, false},
        {RouteId::Root, RouteMethod::Get, "/", RouteMatch::Exact, Access::Public, false},
    };
    char err[64] = {};
    TEST_ASSERT_TRUE(validateRouteTable(t, 3, err, sizeof(err)));
    // The same "/" prefix as a regular route does shadow them.
    t[0].id = RouteId::WifiStatus;
    TEST_ASSERT_FALSE(validateRouteTable(t, 3, err, sizeof(err)));
}

// routeSpec() never falls back to another row (final-check Should 1): every
// table row is found as itself; documentation-only ids give nullptr from
// findRouteSpec() and the fail-closed ROUTE_NOT_FOUND from routeSpec().
static void web_auth_test_routes_spec_not_found_is_sentinel() {
    size_t n = 0;
    const RouteSpec* t = routeTable(n);
    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE(findRouteSpec(t[i].id) == &t[i]);
        TEST_ASSERT_TRUE(&routeSpec(t[i].id) == &t[i]);
    }
    const RouteId docOnly[] = {RouteId::ElegantOtaStart, RouteId::ElegantOtaUpload, RouteId::Static};
    for (RouteId id : docOnly) {
        TEST_ASSERT_NULL(findRouteSpec(id));
        const RouteSpec& s = routeSpec(id);
        TEST_ASSERT_TRUE(&s == &ROUTE_NOT_FOUND);
        TEST_ASSERT_FALSE(&s == &routeSpec(RouteId::GuardFormBody));
        // Fail closed: strictest access, POST only, a path nothing matches.
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Access::Ota), static_cast<int>(s.access));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteMethod::Post), static_cast<int>(s.method));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(RouteMatch::Exact), static_cast<int>(s.match));
        TEST_ASSERT_EQUAL_STRING("\n", s.path);
    }
    // A corrupt id outside the enum is not found either.
    TEST_ASSERT_NULL(findRouteSpec(static_cast<RouteId>(0xEE)));
    TEST_ASSERT_TRUE(&routeSpec(static_cast<RouteId>(0xEE)) == &ROUTE_NOT_FOUND);
}

static void web_auth_test_form_guard_refuses_over_cap_everywhere() {
    const size_t over = WEB_FORM_BODY_MAX + 1;
    // Any method, any path: API reads/writes, pages, unknown URLs.
    TEST_ASSERT_TRUE(formBodyGuardRefuses(true, "/api/login", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(false, "/api/state", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(false, "/", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(true, "/setup", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(false, "/update/x", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(true, "/no/such/path", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(true, "/ota", over));   // not under the /ota/ prefix
    // Non-origin-form targets (kept verbatim in url() by the library): the
    // asterisk form and absolute-form are not /ota/-exempt, so refused.
    // (On device GuardFormBody's "/" prefix maps to AsyncURIMatcher::all()
    // in routeMatcher, so these reach the filter; the guard also ignores the
    // WS/SSE connection type in BodyGuardHandler::canHandle - neither is
    // expressible natively.)
    TEST_ASSERT_TRUE(formBodyGuardRefuses(true, "*", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(false, "*", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(true, "http://h/ota/start", over));
    TEST_ASSERT_TRUE(formBodyGuardRefuses(false, "http://h/api/backup/import", over));
    // A non-POST to the import URL is not the import route's body.
    TEST_ASSERT_TRUE(formBodyGuardRefuses(false, "/api/backup/import", over));
    // At or under the cap: never refused.
    TEST_ASSERT_FALSE(formBodyGuardRefuses(true, "/api/login", WEB_FORM_BODY_MAX));
    TEST_ASSERT_FALSE(formBodyGuardRefuses(false, "/", 0));
    // Exempt owners: POST import (own 8192 cap) and /ota/* (OtaAny).
    TEST_ASSERT_FALSE(formBodyGuardRefuses(true, "/api/backup/import", over));
    TEST_ASSERT_FALSE(formBodyGuardRefuses(true, "/ota/upload", 1000000));
    TEST_ASSERT_FALSE(formBodyGuardRefuses(false, "/ota/start", over));
    TEST_ASSERT_FALSE(formBodyGuardRefuses(true, nullptr, over));
}

// Runs every test in this suite. Call from runWebSuite().
inline void runWebAuthSuite() {
    RUN_TEST(web_auth_test_jsonout_escaping);
    RUN_TEST(web_auth_test_jsonout_nesting_commas);
    RUN_TEST(web_auth_test_jsonout_overflow_latch);
    RUN_TEST(web_auth_test_jsonout_nan_to_null);

    RUN_TEST(web_auth_test_session_create_authorize_levels);
    RUN_TEST(web_auth_test_session_bad_csrf);
    RUN_TEST(web_auth_test_session_ota_grant_expires);
    RUN_TEST(web_auth_test_session_24h_expiry);
    RUN_TEST(web_auth_test_session_epoch_change_invalidates);
    RUN_TEST(web_auth_test_session_evicts_oldest_when_full);
    RUN_TEST(web_auth_test_session_revoke_and_revoke_all);
    RUN_TEST(web_auth_test_session_token_uniqueness);
    RUN_TEST(web_auth_test_session_create_fails_without_rng);
    RUN_TEST(web_auth_test_session_csrf_for_and_remaining_s);

    RUN_TEST(web_auth_test_cookie_parse_among_several);
    RUN_TEST(web_auth_test_cookie_prefix_name_trap);
    RUN_TEST(web_auth_test_cookie_overflow);
    RUN_TEST(web_auth_test_cookie_malformed_segments);
    RUN_TEST(web_auth_test_cookie_whitespace);
    RUN_TEST(web_auth_test_cookie_very_long_header);
    RUN_TEST(web_auth_test_cookie_format_session_cookie);

    RUN_TEST(web_auth_test_throttle_locks_after_fifth_failure);
    RUN_TEST(web_auth_test_throttle_success_resets);

    RUN_TEST(web_auth_test_board_pending_done_unknown);
    RUN_TEST(web_auth_test_board_evicts_after_capacity);
    RUN_TEST(web_auth_test_board_record_unexpected_ignored);

    RUN_TEST(web_auth_test_export_slot_full_lifecycle);
    RUN_TEST(web_auth_test_export_slot_build_failure_goes_idle);
    RUN_TEST(web_auth_test_export_slot_ttl_expiry_returns_buffer);

    RUN_TEST(web_auth_test_routes_real_table_validates);
    RUN_TEST(web_auth_test_routes_duplicate_fails);
    RUN_TEST(web_auth_test_routes_prefix_without_trailing_slash_fails);
    RUN_TEST(web_auth_test_routes_post_with_read_fails);
    RUN_TEST(web_auth_test_routes_get_with_write_fails);
    RUN_TEST(web_auth_test_routes_shadowed_exact_fails);
    RUN_TEST(web_auth_test_routes_form_guard_is_global);
    RUN_TEST(web_auth_test_routes_spec_not_found_is_sentinel);
    RUN_TEST(web_auth_test_form_guard_refuses_over_cap_everywhere);
}
