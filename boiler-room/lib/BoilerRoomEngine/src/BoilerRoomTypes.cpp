#include "BoilerRoomTypes.h"

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

const char* pumpReasonKey(PumpReason r) {
    switch (r) {
        case PumpReason::None: return "none";
        case PumpReason::SensorWait: return "wait";
        case PumpReason::Charge: return "charge";
        case PumpReason::ChargeT1Only: return "charge_t1";
        case PumpReason::Overheat: return "overheat";
        case PumpReason::T1FaultForced: return "t1_fault";
        case PumpReason::Return: return "return";
        case PumpReason::ReturnT2Only: return "return_t2";
        case PumpReason::ReturnBurnGate: return "burn_gate";
        case PumpReason::T1T2FaultForced: return "t1t2_fault";
        case PumpReason::SupplyNormal: return "normal";
        case PumpReason::SupplyOff: return "off";
        case PumpReason::SupplyOffer: return "offer";
        case PumpReason::AntiFreeze: return "anti_freeze";
        case PumpReason::OverheatDump: return "dump";
    }
    return "none";
}

const char* pumpReasonShort(PumpReason r) {
    switch (r) {
        case PumpReason::None: return "-";
        case PumpReason::SensorWait: return "wait";
        case PumpReason::Charge: return "charge";
        case PumpReason::ChargeT1Only: return "T1 only";
        case PumpReason::Overheat: return "overheat";
        case PumpReason::T1FaultForced: return "T1 fail";
        case PumpReason::Return: return "return";
        case PumpReason::ReturnT2Only: return "T2 only";
        case PumpReason::ReturnBurnGate: return "burn gate";
        case PumpReason::T1T2FaultForced: return "T1T2 fail";
        case PumpReason::SupplyNormal: return "normal";
        case PumpReason::SupplyOff: return "off";
        case PumpReason::SupplyOffer: return "offer";
        case PumpReason::AntiFreeze: return "antifrz";
        case PumpReason::OverheatDump: return "dump";
    }
    return "-";
}

const char* p3ModeKey(P3Mode m) {
    switch (m) {
        case P3Mode::Normal: return "normal";
        case P3Mode::Off: return "off";
        case P3Mode::Offer: return "offer";
    }
    return "none";
}

const char* p3ModeShort(P3Mode m) {
    switch (m) {
        case P3Mode::Normal: return "NORMAL";
        case P3Mode::Off: return "OFF";
        case P3Mode::Offer: return "OFFER";
    }
    return "-";
}
