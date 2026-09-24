#include "NetJson.h"

#include <ArduinoJson.h>

const char* netWifiModeName(NetWifiMode m) {
    switch (m) {
        case NetWifiMode::Off:
            return "off";
        case NetWifiMode::Station:
            return "station";
        case NetWifiMode::SetupAp:
            return "setup_ap";
        case NetWifiMode::SetupApJoining:
            return "setup_ap_joining";
    }
    return "off";
}

namespace {

// snake_case, matching netWifiModeName()'s style. Local to this file: no
// other stage-04 module needs the string form of OtaBootOutcome.
const char* otaBootOutcomeName(OtaBootOutcome o) {
    switch (o) {
        case OtaBootOutcome::Normal:
            return "normal";
        case OtaBootOutcome::Trial:
            return "trial";
        case OtaBootOutcome::UpdatedNoRollback:
            return "updated_no_rollback";
        case OtaBootOutcome::RolledBack:
            return "rolled_back";
    }
    return "normal";
}

}  // namespace

size_t buildWifiStatusJson(const CommonState& s, char* out, size_t cap) {
    const NetworkStatus& n = s.network;
    JsonDocument doc;
    doc["mode"] = netWifiModeName(n.wifiMode);
    doc["connected"] = n.wifiConnected;
    doc["ssid"] = n.ssid;
    doc["ip"] = n.ip;
    doc["rssi"] = n.wifiRssi;
    doc["hostname"] = n.hostname;
    doc["apActive"] = n.setupApActive;
    doc["apSsid"] = n.apSsid;
    doc["apIp"] = n.apIp;
    doc["mqttEnabled"] = n.mqttEnabled;
    doc["mqttConnected"] = n.mqttConnected;
    doc["scanRunning"] = n.scanRunning;

    if (measureJson(doc) + 1 > cap) {
        return 0;
    }
    return serializeJson(doc, out, cap);
}

size_t buildScanJson(const WifiScanList& list, bool running, char* out, size_t cap) {
    JsonDocument doc;
    doc["running"] = running;
    JsonArray networks = doc["networks"].to<JsonArray>();
    if (measureJson(doc) + 1 > cap) {
        return 0;  // not even the empty list fits
    }
    // Entries are RSSI-sorted (strongest first); add them one by one and drop
    // the first one that would overflow `cap` together with every weaker one,
    // so heavily escaped SSIDs never make the whole snapshot fail (and leave a
    // stale "running":true behind) -- final-check S5.
    for (size_t i = 0; i < list.count(); ++i) {
        const WifiScanEntry& e = list.entry(i);
        JsonObject n = networks.add<JsonObject>();
        n["ssid"] = e.ssid;
        n["rssi"] = e.rssi;
        n["secure"] = e.secure;
        if (measureJson(doc) + 1 > cap) {
            networks.remove(networks.size() - 1);
            break;
        }
    }
    return serializeJson(doc, out, cap);
}

size_t buildVersionJson(const CommonState& s, const char* project, char* out, size_t cap) {
    JsonDocument doc;
    doc["project"] = project != nullptr ? project : "";
    doc["fw"] = s.versions.fw;
    doc["web"] = s.versions.web;
    doc["mismatch"] = s.versions.webMismatch;

    JsonObject ota = doc["ota"].to<JsonObject>();
    ota["inProgress"] = s.ota.inProgress;
    ota["pendingVerify"] = s.ota.pendingVerify;
    ota["bootOutcome"] = otaBootOutcomeName(s.ota.bootOutcome);

    if (measureJson(doc) + 1 > cap) {
        return 0;
    }
    return serializeJson(doc, out, cap);
}
