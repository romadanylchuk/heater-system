#pragma once
#include <NetIdentity.h>

// boiler-room's network identity (stage 04): MQTT topic prefix, mDNS/DHCP
// hostname (boiler-room.local), HA unique-id prefix, device name/model and the
// open setup-AP SSID used only while no Wi-Fi SSID is saved.
inline constexpr NetIdentity BOILER_ROOM_NET = {
    "boiler-room", "boiler_room", "Boiler room", "KC868-A6 boiler-room", "BoilerRoom-Setup"};
