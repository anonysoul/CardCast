#include "WifiConnection.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <lwip/dns.h>

namespace wifi {
namespace {
constexpr int connectionAttempts = 110;
constexpr uint32_t connectionPollMs = 100;

// Run synchronously in the TCP/IP thread before applying the new configuration.
// ESP-IDF 4.4 rejects 0.0.0.0 in esp_netif_set_dns_info; lwIP accepts nullptr
// to clear a server. Clear every slot, including the fallback DNS server.
esp_err_t clearWifiDns(void*) {
    for (int index = 0; index < DNS_MAX_SERVERS; ++index) {
        dns_setserver(index, nullptr);
        if (!ip_addr_isany(dns_getserver(index))) return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace

const char* connect(const WifiSettings& settings) {
    const char* error = validateSettings(settings);
    if (error) {
        return error;
    }
    WiFi.disconnect();
    if (!WiFi.mode(WIFI_STA)) {
        return "Failed to start Wi-Fi";
    }
    // Arduino skips empty DNS arguments, so clear previous servers first.
    const esp_err_t dnsResult = esp_netif_tcpip_exec(clearWifiDns, nullptr);
    if (dnsResult != ESP_OK) {
        log_e("DNS reset failed: %s", esp_err_to_name(dnsResult));
        return "Failed to clear previous DNS";
    }
    IPAddress ip, gateway, mask, dns1, dns2;
    if (settings.staticIP) {
        ip.fromString(settings.ip);
        gateway.fromString(settings.gateway);
        mask.fromString(settings.mask);
        if (!settings.dns1.isEmpty()) dns1.fromString(settings.dns1);
        if (!settings.dns2.isEmpty()) dns2.fromString(settings.dns2);
    }
    // Zero addresses explicitly restore DHCP after a static connection.
    if (!WiFi.config(ip, gateway, mask, dns1, dns2)) {
        return "Failed to apply network settings";
    }
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    for (int attempt = 0; attempt < connectionAttempts; ++attempt) {
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) return nullptr;
        delay(connectionPollMs);
    }
    WiFi.disconnect();
    return "Connection failed. Check settings.";
}

} // namespace wifi
