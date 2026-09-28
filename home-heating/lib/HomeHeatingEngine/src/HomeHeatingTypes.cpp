#include "HomeHeatingTypes.h"

SensorHealth classifySensor(SensorState s) {
    switch (s) {
        case SensorState::Ok:
            return SensorHealth::Ok;
        case SensorState::Unknown:
            return SensorHealth::Pending;
        case SensorState::Fault:
        case SensorState::Unassigned:
        default:
            return SensorHealth::Failed;
    }
}

FailMode computeFailMode(SensorHealth h1, SensorHealth h2, SensorHealth h3) {
    const bool f1 = h1 == SensorHealth::Failed;
    const bool f2 = h2 == SensorHealth::Failed;
    const bool f3 = h3 == SensorHealth::Failed;
    const int failed = (f1 ? 1 : 0) + (f2 ? 1 : 0) + (f3 ? 1 : 0);
    if (failed >= 2) {
        return FailMode::Multi;
    }
    if (f3) {
        return FailMode::H3;
    }
    if (f2) {
        return FailMode::H2;
    }
    if (f1) {
        return FailMode::H1;
    }
    return FailMode::None;
}

const char* p4ReasonKey(P4Reason r) {
    switch (r) {
        case P4Reason::None: return "none";
        case P4Reason::HeatingOff: return "heating_off";
        case P4Reason::SensorWait: return "sensor_wait";
        case P4Reason::Demand: return "demand";
        case P4Reason::OffDelay: return "off_delay";
        case P4Reason::SupplyCold: return "supply_cold";
        case P4Reason::H3FaultForced: return "h3_fault";
        case P4Reason::MultiFaultForced: return "multi_fault";
        case P4Reason::MultiFaultOff: return "multi_fault_off";
        default: break;
    }
    return "none";
}

const char* p4ReasonShort(P4Reason r) {
    switch (r) {
        case P4Reason::None: return "-";
        case P4Reason::HeatingOff: return "heat off";
        case P4Reason::SensorWait: return "wait";
        case P4Reason::Demand: return "demand";
        case P4Reason::OffDelay: return "delay";
        case P4Reason::SupplyCold: return "cold";
        case P4Reason::H3FaultForced: return "H3 fail";
        case P4Reason::MultiFaultForced: return "multi on";
        case P4Reason::MultiFaultOff: return "multi";
        default: break;
    }
    return "?";
}

const char* k2ReasonKey(K2Reason r) {
    switch (r) {
        case K2Reason::None: return "none";
        case K2Reason::SensorWait: return "sensor_wait";
        case K2Reason::Charging: return "charging";
        case K2Reason::DeltaLow: return "delta_low";
        case K2Reason::H3Low: return "h3_low";
        case K2Reason::H4Full: return "h4_full";
        case K2Reason::H3Fault: return "h3_fault";
        case K2Reason::H4Fault: return "h4_fault";
        default: break;
    }
    return "none";
}

const char* k2ReasonShort(K2Reason r) {
    switch (r) {
        case K2Reason::None: return "-";
        case K2Reason::SensorWait: return "wait";
        case K2Reason::Charging: return "charging";
        case K2Reason::DeltaLow: return "dT low";
        case K2Reason::H3Low: return "H3 low";
        case K2Reason::H4Full: return "H4 full";
        case K2Reason::H3Fault: return "H3 fail";
        case K2Reason::H4Fault: return "H4 fail";
        default: break;
    }
    return "?";
}

const char* k1ModeKey(K1Mode m) {
    switch (m) {
        case K1Mode::Unknown: return "unknown";
        case K1Mode::Recalibrating: return "recal";
        case K1Mode::Closed: return "closed";
        case K1Mode::Wait: return "wait";
        case K1Mode::Normal: return "normal";
        case K1Mode::FeedbackOnly: return "fb_only";
        case K1Mode::FeedforwardOnly: return "ff_only";
        case K1Mode::FailPosFeedback: return "failpos_fb";
        case K1Mode::FailPosFixed: return "failpos_fixed";
        default: break;
    }
    return "none";
}

const char* k1ModeShort(K1Mode m) {
    switch (m) {
        case K1Mode::Unknown: return "unknown";
        case K1Mode::Recalibrating: return "recal";
        case K1Mode::Closed: return "closed";
        case K1Mode::Wait: return "wait";
        case K1Mode::Normal: return "normal";
        case K1Mode::FeedbackOnly: return "FB only";
        case K1Mode::FeedforwardOnly: return "FF only";
        case K1Mode::FailPosFeedback: return "fail FB";
        case K1Mode::FailPosFixed: return "fail pos";
        default: break;
    }
    return "?";
}

const char* failModeKey(FailMode f) {
    switch (f) {
        case FailMode::None: return "none";
        case FailMode::H1: return "h1";
        case FailMode::H2: return "h2";
        case FailMode::H3: return "h3";
        case FailMode::Multi: return "multi";
        default: break;
    }
    return "none";
}
