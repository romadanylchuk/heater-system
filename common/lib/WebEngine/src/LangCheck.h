#pragma once
#include <stddef.h>
#include <stdint.h>

// Language-file parity check (D13): common/web/lang/en.json and uk.json must
// be flat JSON objects of non-empty strings with identical key sets. The
// same rule is enforced at buildfs by web_assemble.py; this pure version is
// native-tested against the real files.
//
// Results:
//   ParseErrorA / ParseErrorB  the text is not valid JSON;
//   NotObject                  a document is not an object, or a value is not
//                              a string (the files must be flat);
//   EmptyValue                 a value is "";
//   MissingInB / MissingInA    a key of one file is absent from the other.
// The checks run in that order (a before b for the per-document checks).
enum class LangCheckResult : uint8_t { Ok, ParseErrorA, ParseErrorB, NotObject, MissingInA, MissingInB, EmptyValue };

// firstKey (may be null when cap == 0) gets the offending key, truncated to
// cap-1 and NUL-terminated; "" when no key applies (parse error, not an object).
LangCheckResult compareLangJson(const char* a, size_t aLen, const char* b, size_t bLen, char* firstKey, size_t cap);

// "ok", "parse_error_a", "parse_error_b", "not_object", "missing_in_a",
// "missing_in_b", "empty_value".
const char* langCheckKey(LangCheckResult r);
