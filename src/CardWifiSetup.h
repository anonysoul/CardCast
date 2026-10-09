/**
 * @file CardWifiSetup.h
 * @author Aurélio Avanzi Dutch version Roland Breedveld adapted by Milton Matuda for Mic WebServer
 * @brief https://github.com/cyberwisk/M5Card_Wifi_KeyBoard_Setup/tree/main/M5Card_Wifi_KeyBoard_Setup
 * @version Apha 0.4BR
 * @date 2024-11-14
 *
 * @Hardwares: M5Cardputer - https://docs.m5stack.com/en/core/Cardputer
 * @Dependent Librarys:
 * M5Cardputer: https://github.com/m5stack/M5Cardputer
 * WiFi: https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi
 **/

#pragma once

#include <WiFi.h>
#include <Preferences.h>
#include <esp_netif.h>
#include <lwip/dns.h>
#include "NetworkValidation.h"
#define NVS_SSID_KEY "wifi_ssid"
#define NVS_PASS_KEY "wifi_pass"

Preferences preferences;

// Do not let the Enter key from the previous screen submit this screen.
void waitKeyboardRelease() {
    do {
        M5Cardputer.update();
        delay(20);
    } while (M5Cardputer.Keyboard.isPressed());
}

String inputText(int x, int y, const String& initial = "") {
    waitKeyboardRelease();
    String data = "> " + initial;

    M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextScroll(true);
    M5Cardputer.Display.drawString(data.length() > 37 ? data.substring(data.length() - 37) : data, x, y);

    while (1) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange()) {
            if (M5Cardputer.Keyboard.isPressed()) {
                Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
                for (auto i : status.word) {
                    data += i;
                }
                if (status.del && data.length() > 2) {
                    data.remove(data.length() - 1);
                }
                if (status.enter) {
                    data.remove(0, 2);
                    return data;
                }
                M5Cardputer.Display.fillRect(0, y - 4, M5Cardputer.Display.width(), 25, BLACK);
                M5Cardputer.Display.drawString(data.length() > 37 ? data.substring(data.length() - 37) : data, 4, y);
            }
        }
        delay(20);
    }
}

int scanWifiNetworks() {
    int networksCount = 0;

    while (networksCount <= 0) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setTextColor(TFT_DARKCYAN);
        M5Cardputer.Display.setTextSize(1.6);
        M5Cardputer.Display.drawString("Scanning Networks", 42, 55);
        M5Cardputer.Display.setTextColor(TFT_LIGHTGREY);
        M5Cardputer.Display.drawString("Connect to WiFi", 52, 75);
        networksCount = WiFi.scanNetworks();

        if (networksCount <= 0) {
            M5Cardputer.Display.drawString("No networks found", 45, 95);
            delay(2000);
        }
    }
    
    return networksCount;
}

static void drawWifiList(int numNetworks, int topIndex, int selectedIndex, int x, int y0, int visibleRows, int rowHeight)
{
    bool showBar = (numNetworks > visibleRows);
    int screenWidth = M5Cardputer.Display.width();
    int listHeight = visibleRows * rowHeight;
    int vbarWidth = showBar ? 8 : 0;
    int vbarX = screenWidth - vbarWidth;
    int listWidth = screenWidth - vbarWidth - 2;

    // Clear everything below the header
    M5Cardputer.Display.fillRect(0, y0 - 2, screenWidth, listHeight + 4, TFT_BLACK);

    // Draw visible rows
    M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    for (int row = 0; row < visibleRows; row++) {
        int ssidIndex = topIndex + row;
        if (ssidIndex >= numNetworks) 
            break;

        int y = y0 + row * rowHeight;
        String ssid = WiFi.SSID(ssidIndex);
        String line;
        if (ssidIndex == selectedIndex)
            line = "-> " + ssid;
        else 
            line = "   " + ssid;

        M5Cardputer.Display.drawString(line, x, y);
    }

    if (!showBar)
        return;

    // Overwrite scrollbar on top of the text
    M5Cardputer.Display.fillRect(vbarX, y0 - 2, vbarWidth, listHeight + 4, TFT_BLACK);
    if (numNetworks > visibleRows) {
        int trackX = vbarX + 1;
        int trackY = y0;
        int trackWidth = vbarWidth - 2;
        int trackHeight = listHeight;

        // Scrollbar background
        M5Cardputer.Display.fillRect(trackX, trackY, trackWidth, trackHeight, TFT_DARKGREY);
        M5Cardputer.Display.drawRect(trackX, trackY, trackWidth, trackHeight, 0x05A3);

        // Scrollbar thumb
        int minThumb = 12;
        int thumbHeigth = (trackHeight * visibleRows) / numNetworks;
        if (thumbHeigth < minThumb) thumbHeigth = minThumb;

        // Thumb position maps scroll 'top' into [0 .. trackH - thumbH]
        int scrollRange = numNetworks - visibleRows;
        int thumbY = trackY;
        if (scrollRange > 0)
            thumbY = trackY + ((trackHeight - thumbHeigth) * topIndex) / scrollRange;

        // Draw scrollbar thumb
        M5Cardputer.Display.fillRect(trackX + 1, thumbY + 1, trackWidth - 2, thumbHeigth - 2, 0x05A3);
    }
}

String selectWifiNetwork(int numNetworks)
{
    waitKeyboardRelease();
    int visibleRows = 6;
    int rowHeight = 18;
    int X = 1;
    int Y0 = 18;
    int selectedIndex = 0;  // Row which is currently selected
    int topIndex = 0;       // The first row on the list
    // The visible window is comprised of:
    // ===HEADER===                 
    // topIndex                     |S|
    // indexItem1                   |C|
    // indexItem2                   |R|
    // indexItem3                   |O|
    // indexItem4                   |L|
    // topIndex+visibleRows - 1     |L|

    // Select network header
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_DARKCYAN, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1.6);
    M5Cardputer.Display.drawString("Select Network", 1, 1);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);

    drawWifiList(numNetworks, topIndex, selectedIndex, X, Y0, visibleRows, rowHeight);

    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto inputKey = M5Cardputer.Keyboard.keysState();

            // Up (';')
            if (M5Cardputer.Keyboard.isKeyPressed(';') && selectedIndex > 0) {
                selectedIndex--;
                // Means that when I press 'up' the top item isn't the top item anymore, it's second to selectedIndex, so replace it
                if (selectedIndex < topIndex) 
                    topIndex = selectedIndex;
                drawWifiList(numNetworks, topIndex, selectedIndex, X, Y0, visibleRows, rowHeight);
            }

            // Down ('.')
            if (M5Cardputer.Keyboard.isKeyPressed('.') && selectedIndex < (numNetworks - 1)) {
                selectedIndex++;
                // When you press 'down' the top item is not seen anymore, so it needs to be updated
                if (selectedIndex >= topIndex + visibleRows)
                    topIndex = selectedIndex - visibleRows + 1;
                drawWifiList(numNetworks, topIndex, selectedIndex, X, Y0, visibleRows, rowHeight);
            }

            if (inputKey.enter) {
                return WiFi.SSID(selectedIndex);
            }
        }
        delay(20);
    }
}

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

// Keep the legacy SSID/password keys so existing firmware settings still load.
WifiSettings getWifiSettings() {
    WifiSettings settings;
    if (!preferences.begin("wifi_settings", true)) return settings;
    settings.ssid = preferences.getString(NVS_SSID_KEY, "");
    settings.password = preferences.getString(NVS_PASS_KEY, "");
    settings.staticIP = preferences.getBool("static_ip", false);
    settings.ip = preferences.getString("ip", "");
    settings.mask = preferences.getString("mask", "255.255.255.0");
    settings.gateway = preferences.getString("gateway", "");
    settings.dns1 = preferences.getString("dns1", "");
    settings.dns2 = preferences.getString("dns2", "");
    preferences.end();
    return settings;
}

bool setWifiSettings(const WifiSettings& settings) {
    if (!preferences.begin("wifi_settings", false)) return false;
    // Empty strings are valid for open Wi-Fi and optional DNS fields.
    preferences.putString(NVS_SSID_KEY, settings.ssid);
    preferences.putString(NVS_PASS_KEY, settings.password);
    preferences.putBool("static_ip", settings.staticIP);
    preferences.putString("ip", settings.ip);
    preferences.putString("mask", settings.mask);
    preferences.putString("gateway", settings.gateway);
    preferences.putString("dns1", settings.dns1);
    preferences.putString("dns2", settings.dns2);
    bool saved = preferences.getString(NVS_SSID_KEY) == settings.ssid &&
        preferences.getString(NVS_PASS_KEY) == settings.password &&
        preferences.getBool("static_ip") == settings.staticIP &&
        preferences.getString("ip") == settings.ip &&
        preferences.getString("mask") == settings.mask &&
        preferences.getString("gateway") == settings.gateway &&
        preferences.getString("dns1") == settings.dns1 &&
        preferences.getString("dns2") == settings.dns2;
    preferences.end();
    return saved;
}

void wifiMessage(const String& message) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.drawString(message, 4, 45);
    M5Cardputer.Display.drawString("Press any key", 4, 100);
    waitKeyboardRelease();
    while (!M5Cardputer.Keyboard.isPressed()) {
        M5Cardputer.update();
        delay(20);
    }
}

int wifiMenu(const String& title, const String* items, int count, int selected = 0) {
    waitKeyboardRelease();
    bool redraw = true;
    while (true) {
        if (redraw) {
            M5Cardputer.Display.fillScreen(TFT_BLACK);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setTextColor(TFT_DARKCYAN, TFT_BLACK);
            M5Cardputer.Display.drawString(title.substring(0, 38), 4, 4);
            M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
            for (int i = 0; i < count; ++i)
                M5Cardputer.Display.drawString((i == selected ? "> " : "  ") + items[i], 4, 24 + i * 18);
            M5Cardputer.Display.drawString("; Up  . Down  Enter Select", 4, 122);
            redraw = false;
        }
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            if (M5Cardputer.Keyboard.keysState().enter) return selected;
            if (M5Cardputer.Keyboard.isKeyPressed(';') && selected > 0) {
                --selected;
                redraw = true;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.') && selected < count - 1) {
                ++selected;
                redraw = true;
            }
        }
        delay(20);
    }
}

String editWifiField(const String& label, const String& value, bool optional = false) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_DARKCYAN, TFT_BLACK);
    M5Cardputer.Display.drawString(label, 4, 4);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5Cardputer.Display.drawString("Del: erase   Enter: accept", 4, 55);
    if (optional) M5Cardputer.Display.drawString("May be left empty", 4, 75);
    return inputText(4, 108, value);
}

const char* validateWifiSettings(const WifiSettings& settings) {
    if (!settings.staticIP) return nullptr;
    return validateStaticNetwork(settings.ip.c_str(), settings.mask.c_str(),
        settings.gateway.c_str(), settings.dns1.c_str(), settings.dns2.c_str());
}

void editNetworkSettings(WifiSettings& settings) {
    const String modes[] = {"DHCP", "Static IP", "Back"};
    int mode = wifiMenu("Address mode", modes, 3, settings.staticIP ? 1 : 0);
    if (mode == 2) return;
    if (mode == 0) {
        settings.staticIP = false;
        return;
    }
    WifiSettings draft = settings;
    draft.staticIP = true;
    while (true) {
        draft.ip = editWifiField("IP address", draft.ip);
        draft.mask = editWifiField("Subnet mask", draft.mask);
        draft.gateway = editWifiField("Gateway", draft.gateway);
        draft.dns1 = editWifiField("Primary DNS", draft.dns1, true);
        draft.dns2 = editWifiField("Secondary DNS", draft.dns2, true);
        const char* error = validateWifiSettings(draft);
        if (error) wifiMessage(error);
        const String actions[] = {error ? "Edit again" : "Use settings", error ? "Cancel" : "Edit again", "Cancel"};
        int action = wifiMenu(error ? "Invalid settings" : "Confirm settings", actions, error ? 2 : 3);
        if (error) {
            if (action == 1) return;
        } else {
            if (action == 0) {
                settings = draft;
                return;
            }
            if (action == 2) return;
        }
    }
}

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

bool connectToWifi(const WifiSettings& settings) {
    const char* error = validateWifiSettings(settings);
    if (error) {
        wifiMessage(error);
        return false;
    }
    WiFi.disconnect();
    if (!WiFi.mode(WIFI_STA)) {
        wifiMessage("Failed to start Wi-Fi");
        return false;
    }
    // Arduino skips empty DNS arguments, so clear previous servers first.
    const esp_err_t dnsResult = esp_netif_tcpip_exec(clearWifiDns, nullptr);
    if (dnsResult != ESP_OK) {
        log_e("DNS reset failed: %s", esp_err_to_name(dnsResult));
        wifiMessage("Failed to clear previous DNS");
        return false;
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
        wifiMessage("Failed to apply network settings");
        return false;
    }
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5Cardputer.Display.drawString("Connecting...", 4, 45);
    M5Cardputer.Display.drawString(settings.staticIP ? "Static: " + settings.ip : "DHCP", 4, 65);
    for (int attempt = 0; attempt < 110; ++attempt) {
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) return true;
        delay(100);
    }
    WiFi.disconnect();
    wifiMessage("Connection failed. Check settings.");
    return false;
}

void setupWifi() {
    const WifiSettings saved = getWifiSettings();
    while (true) {
        String ssid = selectWifiNetwork(scanWifiNetworks());
        WifiSettings settings;
        if (saved.ssid == ssid) settings = saved;
        else {
            settings.ssid = ssid;
            settings.password = editWifiField("Wi-Fi password", "", true);
        }
        bool failed = false;
        while (true) {
            const String actions[] = {failed ? "Retry connection" : "Connect",
                "Change password", settings.staticIP ? "Network settings: Static" : "Network settings: DHCP",
                "Select another network"};
            int action = wifiMenu(settings.ssid, actions, 4);
            if (action == 3) break;
            if (action == 1) settings.password = editWifiField("Wi-Fi password", settings.password, true);
            if (action == 2) editNetworkSettings(settings);
            if (action == 0) {
                if (connectToWifi(settings)) {
                    if (!setWifiSettings(settings)) wifiMessage("Connected, but settings not saved");
                    return;
                }
                // Retain both the previous saved profile and this attempt's inputs.
                failed = true;
            }
        }
    }
}
