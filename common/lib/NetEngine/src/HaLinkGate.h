#pragma once
#include <CommonState.h>

// D12 no-need gating: the effective "no HA need" flag a controller reads is
// the persisted setting AND the current effective MQTT link (transport
// connected AND Wi-Fi up, see NetworkStatus::mqttConnected). `mqttConnected`
// is false from boot until the first session, and falls within ~1 s of a
// Wi-Fi drop, because the MQTT session sees Wi-Fi down on the same tick. The
// persisted value itself is never touched by this gate. Stage 07 reads the
// gated value for its no-need logic; stage 04 only provides and tests this
// function against the real boiler-room settings schema.
inline bool haGatedFlag(bool persisted, const NetworkStatus& net) { return persisted && net.mqttConnected; }
