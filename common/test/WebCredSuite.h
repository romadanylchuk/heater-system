#pragma once
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "../lib/WebEngine/src/AdminCredentials.h"
#include "../lib/WebEngine/src/SessionStore.h"

// Native tests for the pure admin credential pair and its epoch (stage 05
// phase 4, review-3 Should-fix 2): verify*() report the epoch of the exact
// generation they checked, so a login that verified the OLD password (a
// snapshot taken before a concurrent credential change) binds its session
// to the OLD epoch and the session is rejected once the change is live.
// Test names are prefixed web_cred_.

namespace {
uint32_t g_webCredRandomCounter = 0;

void webCredFillRandom(uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>((g_webCredRandomCounter * 31 + i * 11 + 3) & 0xFF);
    }
    ++g_webCredRandomCounter;
}
}  // namespace

static void web_cred_test_assign_bumps_epoch_and_verify_reports_it() {
    AdminCredentials c;
    TEST_ASSERT_EQUAL_UINT32(0, c.epoch());
    c.assign("admin", "secret12");
    TEST_ASSERT_EQUAL_UINT32(1, c.epoch());
    uint32_t e = 99;
    TEST_ASSERT_TRUE(c.verify("admin", 5, "secret12", 8, e));
    TEST_ASSERT_EQUAL_UINT32(1, e);
    e = 99;
    TEST_ASSERT_TRUE(c.verifyPassword("secret12", 8, e));
    TEST_ASSERT_EQUAL_UINT32(1, e);
    c.assign("admin", "secret12");  // even an identical re-assign is a new generation
    TEST_ASSERT_EQUAL_UINT32(2, c.epoch());
    TEST_ASSERT_TRUE(c.verify("admin", 5, "secret12", 8, e));
    TEST_ASSERT_EQUAL_UINT32(2, e);
}

static void web_cred_test_mismatch_and_fail_closed() {
    AdminCredentials c;
    uint32_t e = 77;
    TEST_ASSERT_FALSE(c.verify("", 0, "", 0, e));  // empty stored pair never accepted
    TEST_ASSERT_FALSE(c.verifyPassword("", 0, e));
    c.assign("admin", "");
    TEST_ASSERT_FALSE(c.verify("admin", 5, "", 0, e));
    c.assign("admin", "secret12");
    TEST_ASSERT_FALSE(c.verify("admin", 5, "secret1", 7, e));
    TEST_ASSERT_FALSE(c.verify("Admin", 5, "secret12", 8, e));
    TEST_ASSERT_FALSE(c.verify(nullptr, 0, "secret12", 8, e));
    TEST_ASSERT_FALSE(c.verify("admin", 5, nullptr, 0, e));
    TEST_ASSERT_FALSE(c.verifyPassword("secret13", 8, e));
    TEST_ASSERT_FALSE(c.verifyPassword(nullptr, 0, e));
    TEST_ASSERT_EQUAL_UINT32(77, e);  // untouched on every failure
}

static void web_cred_test_truncation_and_null() {
    AdminCredentials c;
    char longPass[AdminCredentials::PASS_MAX + 10];
    memset(longPass, 'p', sizeof(longPass) - 1);
    longPass[sizeof(longPass) - 1] = '\0';
    c.assign(nullptr, longPass);
    TEST_ASSERT_EQUAL_STRING("", c.user());
    TEST_ASSERT_EQUAL_UINT32(AdminCredentials::PASS_MAX, strlen(c.pass()));
    c.wipe();
    TEST_ASSERT_EQUAL_STRING("", c.pass());
    TEST_ASSERT_EQUAL_UINT32(1, c.epoch());  // wipe keeps the generation
}

// The race the review described: the web task copies the pair (as AdminAuth
// does under its lock), the loop task then changes the password, and the
// login finishes against the stale copy. The session must be bound to the
// stale copy's epoch and be refused under the new one.
static void web_cred_test_stale_snapshot_login_cannot_survive_change() {
    AdminCredentials live;
    live.assign("admin", "oldpass1");
    AdminCredentials snapshot = live;   // verify-side copy taken before the change
    live.assign("admin", "newpass1");   // the credential change lands

    uint32_t verifiedEpoch = 0;
    TEST_ASSERT_TRUE(snapshot.verify("admin", 5, "oldpass1", 8, verifiedEpoch));
    TEST_ASSERT_EQUAL_UINT32(1, verifiedEpoch);
    TEST_ASSERT_EQUAL_UINT32(2, live.epoch());

    SessionStore store(&webCredFillRandom);
    SessionTokens tok;
    TEST_ASSERT_TRUE(store.create(1000, verifiedEpoch, tok));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::NoSession),
        static_cast<int>(store.authorize(tok.sid, nullptr, Access::Read, 1001, live.epoch())));

    // The new password verifies against the live pair and yields a session
    // that is valid under the live epoch.
    TEST_ASSERT_TRUE(live.verify("admin", 5, "newpass1", 8, verifiedEpoch));
    TEST_ASSERT_EQUAL_UINT32(2, verifiedEpoch);
    TEST_ASSERT_TRUE(store.create(1002, verifiedEpoch, tok));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GateResult::Ok),
        static_cast<int>(store.authorize(tok.sid, nullptr, Access::Read, 1003, live.epoch())));
    TEST_ASSERT_FALSE(live.verify("admin", 5, "oldpass1", 8, verifiedEpoch));
}

inline void runWebCredSuite() {
    RUN_TEST(web_cred_test_assign_bumps_epoch_and_verify_reports_it);
    RUN_TEST(web_cred_test_mismatch_and_fail_closed);
    RUN_TEST(web_cred_test_truncation_and_null);
    RUN_TEST(web_cred_test_stale_snapshot_login_cannot_survive_change);
}
