#include "SchemaJsonStream.h"

#include <string.h>
#include <SettingDescriptor.h>
#include "JsonOut.h"

namespace {

constexpr uint8_t SCHEMA_DECIMALS = 3;

const char* typeKey(SettingType t) {
    switch (t) {
        case SettingType::Int:   return "int";
        case SettingType::Float: return "float";
        case SettingType::Bool:  return "bool";
        case SettingType::Text:  return "text";
    }
    return "int";
}

}  // namespace

SchemaJsonStream::SchemaJsonStream(const ConfigSchema& schema, const char* project)
    : _schema(schema), _project(project != nullptr ? project : "") {}

size_t SchemaJsonStream::read(char* out, size_t cap) {
    if (out == nullptr || cap == 0) return 0;
    size_t written = 0;
    while (written < cap) {
        if (_pendPos >= _pendLen) {
            _pendLen = 0;
            _pendPos = 0;
            if (!renderNext()) break;
            if (_pendLen == 0) continue;
        }
        size_t n = _pendLen - _pendPos;
        if (n > cap - written) n = cap - written;
        memcpy(out + written, _scratch + _pendPos, n);
        _pendPos += n;
        written += n;
    }
    return written;
}

bool SchemaJsonStream::renderNext() {
    if (_failed) return false;
    switch (_phase) {
        case Phase::Header:
            _phase = Phase::Items;
            if (!renderHeader()) {
                _failed = true;
                _phase = Phase::Done;
                return false;
            }
            return true;
        case Phase::Items:
            while (_table < _schema.tableCount && _item >= _schema.tables[_table].count) {
                ++_table;
                _item = 0;
            }
            if (_table >= _schema.tableCount) {
                _phase = Phase::Footer;
                return renderNext();
            }
            if (!renderItem(_schema.tables[_table].items[_item], _firstItem)) {
                _failed = true;
                _phase = Phase::Done;
                return false;
            }
            _firstItem = false;
            ++_item;
            return true;
        case Phase::Footer:
            _phase = Phase::Done;
            _scratch[0] = ']';
            _scratch[1] = '}';
            _pendLen = 2;
            return true;
        case Phase::Done:
            break;
    }
    return false;
}

bool SchemaJsonStream::renderHeader() {
    // Deliberately left open: `{"project":"..","settings":[`.
    JsonOut j(_scratch, sizeof(_scratch));
    j.beginObject();
    j.key("project");
    j.str(_project);
    j.key("settings");
    j.beginArray();
    if (!j.ok()) return false;
    _pendLen = j.length();
    return true;
}

bool SchemaJsonStream::renderItem(const SettingDescriptor& d, bool first) {
    size_t offset = 0;
    if (!first) {
        _scratch[0] = ',';
        offset = 1;
    }
    JsonOut j(_scratch + offset, sizeof(_scratch) - offset);
    j.beginObject();
    j.key("k");
    j.str(d.key);
    j.key("g");
    j.str(d.group);
    j.key("t");
    j.str(typeKey(d.type));
    j.key("en");
    j.str(d.labelEn);
    j.key("uk");
    j.str(d.labelUa);
    j.key("u");
    j.str(d.unit);   // nullptr -> null
    j.key("min");
    j.num(d.minValue, SCHEMA_DECIMALS);
    j.key("max");
    j.num(d.maxValue, SCHEMA_DECIMALS);
    j.key("step");
    j.num(d.step, SCHEMA_DECIMALS);
    if (d.type == SettingType::Text) {
        j.key("defText");
        j.str(d.defaultText != nullptr ? d.defaultText : "");
        j.key("ml");
        j.integer(d.maxLen);
    } else {
        j.key("def");
        j.num(d.defaultValue, SCHEMA_DECIMALS);
    }
    j.key("secret");
    j.boolean((d.flags & SETTING_FLAG_SECRET) != 0);
    j.endObject();
    if (!j.ok()) return false;
    _pendLen = offset + j.length();
    return true;
}
