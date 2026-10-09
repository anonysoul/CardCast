#pragma once
#include <Arduino.h>

namespace wifi {
struct WifiSettings {
    String ssid;
    String password;
    bool staticIP = false;
    String ip;
    String mask = "255.255.255.0";
    String gateway;
    String dns1;
    String dns2;
};

WifiSettings loadSettings();
bool saveSettings(const WifiSettings& settings);
const char* validateSettings(const WifiSettings& settings);
} // namespace wifi
