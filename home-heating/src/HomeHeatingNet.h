#pragma once
#include <NetIdentity.h>

// home-heating's network identity (stage 04): MQTT topic prefix, mDNS/DHCP
// hostname (home-heating.local), HA unique-id prefix, device name/model and the
// open setup-AP SSID used only while no Wi-Fi SSID is saved.
inline constexpr NetIdentity HOME_HEATING_NET = {
    "home-heating", "home_heating", "Home heating", "KC868-A6 home-heating", "HomeHeating-Setup"};
