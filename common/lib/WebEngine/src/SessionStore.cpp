#include "SessionStore.h"
#include <string.h>
#include "ConstTime.h"

namespace {
constexpr char HEX_DIGITS[] = "0123456789abcdef";
}  // namespace

SessionStore::SessionStore(RandomFill rnd) : _rnd(rnd) {}

bool SessionStore::fillHex(char* out) const {
    if (_rnd == nullptr) {
        return false;
    }
    uint8_t raw[TOKEN_BYTES];
    _rnd(raw, TOKEN_BYTES);
    for (size_t i = 0; i < TOKEN_BYTES; ++i) {
        out[i * 2] = HEX_DIGITS[(raw[i] >> 4) & 0x0F];
        out[i * 2 + 1] = HEX_DIGITS[raw[i] & 0x0F];
    }
    out[TOKEN_HEX_LEN] = '\0';
    return true;
}

void SessionStore::freeSlot(Slot& s) {
    s.used = false;
    memset(s.sid, 0, sizeof(s.sid));
    memset(s.csrf, 0, sizeof(s.csrf));
    s.createdMs = 0;
    s.otaGrantUntilMs = 0;
    s.epoch = 0;
}

void SessionStore::reapStale(uint64_t nowMs, uint32_t epoch) {
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        Slot& s = _slots[i];
        if (s.used && (s.epoch != epoch || expired(s, nowMs))) {
            freeSlot(s);
        }
    }
}

bool SessionStore::create(uint64_t nowMs, uint32_t epoch, SessionTokens& out) {
    if (_rnd == nullptr) {
        return false;
    }
    reapStale(nowMs, epoch);

    int slot = -1;
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        if (!_slots[i].used) {
            slot = static_cast<int>(i);
            break;
        }
    }
    if (slot < 0) {
        // Still full: evict the oldest by createdMs.
        size_t oldest = 0;
        for (size_t i = 1; i < SESSION_SLOTS; ++i) {
            if (_slots[i].createdMs < _slots[oldest].createdMs) {
                oldest = i;
            }
        }
        slot = static_cast<int>(oldest);
    }

    Slot& s = _slots[slot];
    freeSlot(s);
    if (!fillHex(s.sid) || !fillHex(s.csrf)) {
        return false;
    }
    s.used = true;
    s.createdMs = nowMs;
    s.otaGrantUntilMs = 0;
    s.epoch = epoch;

    memcpy(out.sid, s.sid, sizeof(out.sid));
    memcpy(out.csrf, s.csrf, sizeof(out.csrf));
    return true;
}

GateResult SessionStore::authorize(const char* sid, const char* csrf, Access level, uint64_t nowMs, uint32_t epoch) {
    reapStale(nowMs, epoch);

    size_t sidLen = sid != nullptr ? strlen(sid) : 0;
    int matched = -1;
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        Slot& s = _slots[i];
        if (s.used && constTimeEquals(sid, sidLen, s.sid, strlen(s.sid))) {
            matched = static_cast<int>(i);
        }
    }
    if (matched < 0) {
        return GateResult::NoSession;
    }
    Slot& s = _slots[matched];

    if (level == Access::Write || level == Access::Ota) {
        size_t csrfLen = csrf != nullptr ? strlen(csrf) : 0;
        if (!constTimeEquals(csrf, csrfLen, s.csrf, strlen(s.csrf))) {
            return GateResult::BadCsrf;
        }
    }
    if (level == Access::Ota && !(nowMs < s.otaGrantUntilMs)) {
        return GateResult::NoOtaGrant;
    }
    return GateResult::Ok;
}

const SessionStore::Slot* SessionStore::findValid(const char* sid, uint64_t nowMs, uint32_t epoch) const {
    if (sid == nullptr) {
        return nullptr;
    }
    size_t sidLen = strlen(sid);
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        const Slot& s = _slots[i];
        if (s.used && s.epoch == epoch && !expired(s, nowMs) && constTimeEquals(sid, sidLen, s.sid, strlen(s.sid))) {
            return &s;
        }
    }
    return nullptr;
}

bool SessionStore::csrfFor(const char* sid, uint64_t nowMs, uint32_t epoch, char* out, size_t cap) {
    reapStale(nowMs, epoch);
    const Slot* s = findValid(sid, nowMs, epoch);
    if (s == nullptr || cap < sizeof(s->csrf)) {
        return false;
    }
    memcpy(out, s->csrf, sizeof(s->csrf));
    return true;
}

uint32_t SessionStore::remainingS(const char* sid, uint64_t nowMs, uint32_t epoch) const {
    const Slot* s = findValid(sid, nowMs, epoch);
    if (s == nullptr) {
        return 0;
    }
    uint64_t elapsedMs = nowMs - s->createdMs;
    if (elapsedMs >= SESSION_TTL_MS) {
        return 0;
    }
    return static_cast<uint32_t>((SESSION_TTL_MS - elapsedMs) / 1000);
}

bool SessionStore::grantOta(const char* sid, uint64_t nowMs, uint32_t epoch) {
    reapStale(nowMs, epoch);
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        Slot& s = _slots[i];
        if (s.used && s.epoch == epoch && !expired(s, nowMs) && sid != nullptr &&
            constTimeEquals(sid, strlen(sid), s.sid, strlen(s.sid))) {
            s.otaGrantUntilMs = nowMs + OTA_GRANT_TTL_MS;
            return true;
        }
    }
    return false;
}

bool SessionStore::otaGranted(const char* sid, uint64_t nowMs, uint32_t epoch) const {
    const Slot* s = findValid(sid, nowMs, epoch);
    return s != nullptr && nowMs < s->otaGrantUntilMs;
}

void SessionStore::revoke(const char* sid) {
    if (sid == nullptr) {
        return;
    }
    size_t sidLen = strlen(sid);
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        Slot& s = _slots[i];
        if (s.used && constTimeEquals(sid, sidLen, s.sid, strlen(s.sid))) {
            freeSlot(s);
        }
    }
}

void SessionStore::revokeAll() {
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        freeSlot(_slots[i]);
    }
}

size_t SessionStore::activeCount(uint64_t nowMs, uint32_t epoch) const {
    size_t n = 0;
    for (size_t i = 0; i < SESSION_SLOTS; ++i) {
        const Slot& s = _slots[i];
        if (s.used && s.epoch == epoch && !expired(s, nowMs)) {
            ++n;
        }
    }
    return n;
}
