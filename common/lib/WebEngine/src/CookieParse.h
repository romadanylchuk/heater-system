#pragma once
#include <stddef.h>
#include <stdint.h>

// HTTP `Cookie:` header parsing and the session `Set-Cookie:` text (D3).
// Tolerant of the optional spaces around `;` separators used by browsers
// ("a=1; hsid=abc"). `name` must match a whole cookie name -- "xhsid" must
// never match a lookup for "hsid".
bool findCookie(const char* header, const char* name, char* out, size_t cap);

// "hsid=<sid>; Path=/; Max-Age=<maxAgeS>; HttpOnly; SameSite=Strict"
// (maxAgeS 0 clears the cookie on the browser side). Returns the written
// length, or 0 if it would not fit in cap (out left empty in that case).
size_t formatSessionCookie(const char* sid, uint32_t maxAgeS, char* out, size_t cap);
