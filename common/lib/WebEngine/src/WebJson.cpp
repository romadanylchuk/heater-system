#include "WebJson.h"

#include <math.h>
#include <string.h>
#include <EventEntry.h>
#include <EventTypes.h>
#include <SettingDescriptor.h>

namespace {

constexpr size_t LOCAL_TEXT_CAP = 32;
constexpr uint8_t TEMP_DECIMALS = 2;
constexpr uint8_t VALUE_DECIMALS = 2;
// One log entry rendered alone: every field at its absolute maximum (40-char
// floats from JsonOut::num, a 31-char "lt", a 24-char key) stays below this.
constexpr size_t LOG_ENTRY_TMP_CAP = 320;

size_t finish(JsonOut& j) {
    return j.ok() ? j.length() : 0;
}

void keyStr(JsonOut& j, const char* k, const char* v) {
    j.key(k);
    j.str(v);
}

void keyBool(JsonOut& j, const char* k, bool v) {
    j.key(k);
    j.boolean(v);
}

void keyInt(JsonOut& j, const char* k, int64_t v) {
    j.key(k);
    j.integer(v);
}

// Writes the formatted local time of `utc`, or "" when not applicable, no
// formatter, or the formatter fails.
void keyLocalTime(JsonOut& j, const char* k, bool have, uint32_t utc, LocalTimeFormatter fmt, void* ctx) {
    char text[LOCAL_TEXT_CAP] = {};
    if (!have || fmt == nullptr || !fmt(utc, text, sizeof(text), ctx)) text[0] = '\0';
    text[sizeof(text) - 1] = '\0';
    keyStr(j, k, text);
}

const char* timeSourceKey(TimeSourceKind k) {
    switch (k) {
        case TimeSourceKind::Rtc:  return "rtc";
        case TimeSourceKind::Ntp:  return "ntp";
        case TimeSourceKind::None: break;
    }
    return "none";
}

const char* sensorStateKey(SensorState st) {
    switch (st) {
        case SensorState::Unknown:    return "unknown";
        case SensorState::Ok:         return "ok";
        case SensorState::Fault:      return "fault";
        case SensorState::Unassigned: break;
    }
    return "unassigned";
}

const char* otaSourceKey(OtaSource s) {
    switch (s) {
        case OtaSource::Web:    return "web";
        case OtaSource::Espota: return "espota";
        case OtaSource::None:   break;
    }
    return "none";
}

const char* resetPhaseKey(ResetGatePhase p) {
    switch (p) {
        case ResetGatePhase::Countdown: return "countdown";
        case ResetGatePhase::Aborted:   return "aborted";
        case ResetGatePhase::Confirmed: return "confirmed";
        case ResetGatePhase::Inactive:  break;
    }
    return "inactive";
}

// Temperature, or null when not valid (NaN/inf also become null in JsonOut::num).
void keyTemp(JsonOut& j, const char* k, bool valid, float v) {
    j.key(k);
    if (valid) {
        j.num(v, TEMP_DECIMALS);
    } else {
        j.null();
    }
}

// 16 lowercase hex digits in ROM byte order, or "" for an all-zero address.
void keyAddress(JsonOut& j, const char* k, const uint8_t addr[8]) {
    static const char HEX_DIGITS[] = "0123456789abcdef";
    char text[17] = {};
    bool zero = true;
    for (size_t i = 0; i < 8; ++i) {
        if (addr[i] != 0) zero = false;
        text[i * 2] = HEX_DIGITS[addr[i] >> 4];
        text[i * 2 + 1] = HEX_DIGITS[addr[i] & 0x0F];
    }
    keyStr(j, k, zero ? "" : text);
}

size_t visibleSensorCount(const CommonState& s, const HwProjectConfig& hw) {
    size_t n = hw.sensorCount < s.sensors.count ? hw.sensorCount : s.sensors.count;
    return n < MAX_LOGICAL_SENSORS ? n : MAX_LOGICAL_SENSORS;
}

}  // namespace

size_t buildStateJson(const CommonState& s, const WebJsonContext& c, char* out, size_t cap) {
    JsonOut j(out, cap);
    j.beginObject();
    keyStr(j, "project", c.project != nullptr ? c.project : "");
    keyInt(j, "uptimeS", s.system.uptimeS);

    j.key("time");
    j.beginObject();
    keyBool(j, "valid", s.time.valid);
    keyLocalTime(j, "local", s.time.valid, s.time.utcNow, c.fmt, c.fmtCtx);
    keyStr(j, "source", timeSourceKey(s.time.source));
    keyStr(j, "rtc", !s.time.rtcPresent ? "missing" : (s.time.rtcValid ? "ok" : "invalid"));
    keyLocalTime(j, "lastNtp", s.time.lastNtpSyncUtc != 0, s.time.lastNtpSyncUtc, c.fmt, c.fmtCtx);
    j.endObject();

    j.key("net");
    j.beginObject();
    keyBool(j, "wifi", s.network.wifiConnected);
    keyInt(j, "rssi", s.network.wifiRssi);
    keyStr(j, "ip", s.network.ip);
    keyStr(j, "ssid", s.network.ssid);
    keyStr(j, "host", s.network.hostname);
    keyBool(j, "ap", s.network.setupApActive);
    keyStr(j, "apSsid", s.network.apSsid);
    keyStr(j, "apIp", s.network.apIp);
    keyBool(j, "mqttEnabled", s.network.mqttEnabled);
    keyBool(j, "mqtt", s.network.mqttConnected);
    j.endObject();

    keyInt(j, "alarms", s.alarms.activeMask);
    keyInt(j, "warnings", s.diag.warningMask);

    j.key("diag");
    j.beginObject();
    keyBool(j, "nvs", s.diag.nvsError);
    keyBool(j, "rtcMissing", s.diag.rtcMissing);
    keyBool(j, "rtcInvalid", s.diag.rtcInvalid);
    keyBool(j, "tzInvalid", s.diag.tzInvalid);
    keyBool(j, "wdt", s.diag.watchdogError);
    keyBool(j, "queueFull", s.diag.commandQueueFull);
    j.endObject();

    j.key("relays");
    j.beginArray();
    if (c.hw != nullptr) {
        for (size_t i = 0; i < c.hw->relayCount; ++i) {
            const RelayChannelDesc& d = c.hw->relays[i];
            if (d.channel >= RELAY_CHANNEL_COUNT) continue;
            const RelayChannelStatus& st = s.relays.channel[d.channel];
            j.beginObject();
            keyStr(j, "name", d.name);
            keyBool(j, "on", s.relays.on[d.channel]);
            keyBool(j, "req", st.requested);
            keyBool(j, "safety", st.safety);
            keyInt(j, "lockS", st.lockRemainingS);
            j.endObject();
        }
    }
    j.endArray();
    keyBool(j, "inhibited", s.relays.inhibited);

    j.key("sensors");
    j.beginArray();
    if (c.hw != nullptr) {
        size_t n = visibleSensorCount(s, *c.hw);
        for (size_t i = 0; i < n; ++i) {
            const LogicalSensorStatus& ls = s.sensors.sensor[i];
            j.beginObject();
            keyStr(j, "name", c.hw->sensors[i].name);
            keyStr(j, "state", sensorStateKey(ls.state));
            keyTemp(j, "t", ls.state == SensorState::Ok, ls.tempC);
            keyBool(j, "missing", ls.missing);
            j.endObject();
        }
    }
    j.endArray();

    j.key("k1");
    j.beginObject();
    keyBool(j, "present", s.k1.present);
    keyBool(j, "busy", s.k1.busy);
    keyBool(j, "power", s.k1.powerOn);
    keyBool(j, "open", s.k1.directionOpen);
    j.endObject();

    j.key("ota");
    j.beginObject();
    keyBool(j, "inProgress", s.ota.inProgress);
    keyStr(j, "source", otaSourceKey(s.ota.source));
    keyBool(j, "pendingVerify", s.ota.pendingVerify);
    keyInt(j, "verifyS", s.ota.verifyRemainingS);
    j.endObject();

    j.key("ver");
    j.beginObject();
    keyStr(j, "fw", s.versions.fw);
    keyStr(j, "web", s.versions.web);
    keyBool(j, "mismatch", s.versions.webMismatch);
    j.endObject();

    j.key("reset");
    j.beginObject();
    keyStr(j, "phase", resetPhaseKey(s.system.resetPhase));
    keyInt(j, "left", s.system.resetSecondsLeft);
    j.endObject();

    j.key("lastCmd");
    j.beginObject();
    keyInt(j, "id", s.system.lastCommandId);
    keyStr(j, "status", commandStatusKey(static_cast<CommandStatus>(s.system.lastCommandStatus)));
    j.endObject();

    if (c.ext != nullptr) {
        j.key("ctl");
        if (!c.ext(s, j, c.extCtx)) {
            if (cap > 0) out[0] = '\0';
            return 0;
        }
    }
    j.endObject();
    return finish(j);
}

size_t buildSensorsJson(const CommonState& s, const HwProjectConfig& hw, char* out, size_t cap) {
    JsonOut j(out, cap);
    j.beginObject();

    j.key("logical");
    j.beginArray();
    size_t n = visibleSensorCount(s, hw);
    for (size_t i = 0; i < n; ++i) {
        const LogicalSensorStatus& ls = s.sensors.sensor[i];
        j.beginObject();
        keyInt(j, "i", static_cast<int64_t>(i));
        keyStr(j, "name", hw.sensors[i].name);
        keyBool(j, "assigned", ls.assigned);
        keyAddress(j, "addr", ls.address);
        keyStr(j, "state", sensorStateKey(ls.state));
        keyTemp(j, "t", ls.state == SensorState::Ok, ls.tempC);
        keyBool(j, "missing", ls.missing);
        j.endObject();
    }
    j.endArray();

    j.key("bus");
    j.beginArray();
    size_t busCount = s.oneWire.count < ONE_WIRE_MAX_DEVICES ? s.oneWire.count : ONE_WIRE_MAX_DEVICES;
    for (size_t i = 0; i < busCount; ++i) {
        uint8_t logical = s.oneWire.logical[i];
        bool unassigned = logical == NO_LOGICAL_SENSOR;
        j.beginObject();
        keyAddress(j, "addr", s.oneWire.address[i]);
        keyTemp(j, "t", s.oneWire.tempValid[i], s.oneWire.tempC[i]);
        keyInt(j, "logical", unassigned ? -1 : static_cast<int64_t>(logical));
        keyBool(j, "new", unassigned);
        j.endObject();
    }
    j.endArray();

    keyBool(j, "scanDone", s.oneWire.done);
    keyBool(j, "overflow", s.oneWire.overflow);
    keyInt(j, "scanCount", s.oneWire.scanCount);
    j.endObject();
    return finish(j);
}

size_t buildConfigValuesJson(const ConfigEngine& cfg, char* out, size_t cap) {
    JsonOut j(out, cap);
    j.beginObject();

    j.key("v");
    j.beginObject();
    for (size_t i = 0; i < cfg.count(); ++i) {
        const SettingDescriptor* d = cfg.descriptor(i);
        if (d == nullptr || (d->flags & SETTING_FLAG_SECRET) != 0) continue;
        j.key(d->key);
        switch (d->type) {
            case SettingType::Int:   j.integer(cfg.getInt(i)); break;
            case SettingType::Float: j.num(cfg.getNumber(i), VALUE_DECIMALS); break;
            case SettingType::Bool:  j.boolean(cfg.getBool(i)); break;
            case SettingType::Text:  j.str(cfg.getText(i)); break;
        }
    }
    j.endObject();

    j.key("s");
    j.beginObject();
    for (size_t i = 0; i < cfg.count(); ++i) {
        const SettingDescriptor* d = cfg.descriptor(i);
        if (d == nullptr || (d->flags & SETTING_FLAG_SECRET) == 0) continue;
        const char* text = cfg.getText(i);
        keyBool(j, d->key, text != nullptr && text[0] != '\0');
    }
    j.endObject();

    j.endObject();
    return finish(j);
}

size_t buildLogJson(const EventLog& log, const WebJsonContext& c, char* out, size_t cap) {
    // Room kept for the worst tail: ],"truncated":true} plus the NUL.
    constexpr size_t TAIL_RESERVE = sizeof("],\"truncated\":true}");
    JsonOut j(out, cap);
    j.beginObject();
    j.key("events");
    j.beginArray();
    bool truncated = false;
    size_t written = 0;
    for (size_t i = 0; i < log.count() && i < EventLog::CAPACITY; ++i) {
        const EventEntry* e = log.newest(i);
        if (e == nullptr) break;
        // Render the entry on its own first, so a too-long one never leaves
        // a half-written object in the document.
        char entry[LOG_ENTRY_TMP_CAP];
        JsonOut t(entry, sizeof(entry));
        bool rt = (e->flags & EVENT_FLAG_REAL_TIME) != 0;
        t.beginObject();
        keyInt(t, "seq", e->seq);
        keyInt(t, "type", e->type);
        keyStr(t, "key", eventTypeKey(e->type));   // null for project-defined types
        keyInt(t, "src", e->source);
        t.key("val");
        t.num(e->value, VALUE_DECIMALS);
        t.key("aux");
        t.num(e->aux, VALUE_DECIMALS);
        keyInt(t, "rsn", e->reason);
        keyBool(t, "rt", rt);
        keyInt(t, "ts", e->timestamp);
        keyLocalTime(t, "lt", rt, e->timestamp, c.fmt, c.fmtCtx);
        t.endObject();
        size_t need = t.length() + (written > 0 ? 1 : 0);   // + separating comma
        if (!t.ok() || !j.ok() || j.length() + need + TAIL_RESERVE > cap) {
            truncated = true;   // drop this and every older entry
            break;
        }
        j.raw(entry);
        ++written;
    }
    j.endArray();
    if (truncated) keyBool(j, "truncated", true);
    j.endObject();
    return finish(j);
}

size_t buildCmdResultJson(CmdLookup l, const CmdResult& r, uint32_t id, char* out, size_t cap) {
    JsonOut j(out, cap);
    j.beginObject();
    keyInt(j, "id", id);
    switch (l) {
        case CmdLookup::Pending:
            keyBool(j, "done", false);
            break;
        case CmdLookup::Done:
            keyBool(j, "done", true);
            keyStr(j, "status", commandStatusKey(r.status));
            if (r.hasValue) {
                j.key("value");
                j.num(r.value, VALUE_DECIMALS);
            }
            break;
        case CmdLookup::Unknown:
            keyStr(j, "error", "unknown");
            break;
    }
    j.endObject();
    return finish(j);
}

const char* commandStatusKey(CommandStatus st) {
    switch (st) {
        case CommandStatus::Ok:             return "ok";
        case CommandStatus::Clamped:        return "clamped";
        case CommandStatus::Unchanged:      return "unchanged";
        case CommandStatus::Rejected:       return "rejected";
        case CommandStatus::InvalidCommand: return "invalid";
        case CommandStatus::ImportFailed:   return "import_failed";
        case CommandStatus::QueueFull:      return "queue_full";
    }
    return "invalid";
}
