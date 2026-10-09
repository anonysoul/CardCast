#include "WifiSettings.h"
#include "NetworkValidation.h"
#include <Preferences.h>

namespace wifi {
namespace {
constexpr char ssidKey[] = "wifi_ssid";
constexpr char passwordKey[] = "wifi_pass";
}

// Keep the legacy SSID/password keys so existing firmware settings still load.
WifiSettings loadSettings() {
    Preferences preferences;
    WifiSettings settings;
    if (!preferences.begin("wifi_settings", true)) return settings;
    settings.ssid = preferences.getString(ssidKey, "");
    settings.password = preferences.getString(passwordKey, "");
    settings.staticIP = preferences.getBool("static_ip", false);
    settings.ip = preferences.getString("ip", "");
    settings.mask = preferences.getString("mask", "255.255.255.0");
    settings.gateway = preferences.getString("gateway", "");
    settings.dns1 = preferences.getString("dns1", "");
    settings.dns2 = preferences.getString("dns2", "");
    preferences.end();
    return settings;
}

bool saveSettings(const WifiSettings& settings) {
    Preferences preferences;
    if (!preferences.begin("wifi_settings", false)) return false;
    // Empty strings are valid for open Wi-Fi and optional DNS fields.
    preferences.putString(ssidKey, settings.ssid);
    preferences.putString(passwordKey, settings.password);
    preferences.putBool("static_ip", settings.staticIP);
    preferences.putString("ip", settings.ip);
    preferences.putString("mask", settings.mask);
    preferences.putString("gateway", settings.gateway);
    preferences.putString("dns1", settings.dns1);
    preferences.putString("dns2", settings.dns2);
    bool saved = preferences.getString(ssidKey) == settings.ssid &&
        preferences.getString(passwordKey) == settings.password &&
        preferences.getBool("static_ip") == settings.staticIP &&
        preferences.getString("ip") == settings.ip &&
        preferences.getString("mask") == settings.mask &&
        preferences.getString("gateway") == settings.gateway &&
        preferences.getString("dns1") == settings.dns1 &&
        preferences.getString("dns2") == settings.dns2;
    preferences.end();
    return saved;
}

const char* validateSettings(const WifiSettings& settings) {
    if (!settings.staticIP) return nullptr;
    return validateStaticNetwork(settings.ip.c_str(), settings.mask.c_str(),
        settings.gateway.c_str(), settings.dns1.c_str(), settings.dns2.c_str());
}

} // namespace wifi
