#pragma once
#include <stddef.h>
#include <stdint.h>
#include <ConfigSchema.h>

// Resumable byte stream of GET /api/schema over the immutable constexpr
// schema tables (D8): no lock, no large buffer. Produces
// {"project":"..","settings":[{"k","g","t":"int|float|bool|text","en","uk",
// "u"|null,"min","max","step","def"|"defText","ml","secret":b},...]}
// Items are rendered one at a time into an internal 512-B scratch, so any
// read cap >= 1 works and the output is byte-identical whatever the caps.
class SchemaJsonStream {
public:
    static constexpr size_t SCRATCH_BYTES = 512;

    SchemaJsonStream(const ConfigSchema& schema, const char* project);

    // Copies up to cap bytes of the document into out (not NUL-terminated);
    // returns 0 at the end (or after a failure).
    size_t read(char* out, size_t cap);

    // An item did not fit the scratch; the stream stopped early (the output
    // is then truncated/invalid JSON). Never happens with the real tables.
    bool failed() const { return _failed; }

private:
    enum class Phase : uint8_t { Header, Items, Footer, Done };

    bool renderNext();   // fills _scratch with the next piece; false when done/failed
    bool renderHeader();
    bool renderItem(const SettingDescriptor& d, bool first);

    const ConfigSchema& _schema;
    const char* _project;
    Phase _phase = Phase::Header;
    size_t _table = 0;
    size_t _item = 0;
    bool _firstItem = true;
    bool _failed = false;

    char _scratch[SCRATCH_BYTES] = {};
    size_t _pendLen = 0;
    size_t _pendPos = 0;
};
