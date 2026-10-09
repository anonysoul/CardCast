/**
 * Wi-Fi setup originally based on Aurélio Avanzi's keyboard setup,
 * with adaptations by Roland Breedveld and Milton Matuda.
 * https://github.com/cyberwisk/M5Card_Wifi_KeyBoard_Setup/tree/main/M5Card_Wifi_KeyBoard_Setup
 */
#include "CardWifiSetup.h"
#include "CardDisplay.h"
#include "CardInput.h"
#include "CardWifiView.h"
#include "WifiConnection.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <algorithm>
#include <cstring>

using wifi::WifiSettings;
namespace view = card_wifi_view;
using card_input::Navigation;

namespace {
int scanWifiNetworks() {
    // Keep the home screen visible during both initial scans and network reselection.
    card_display::showWelcome();
    int networksCount = 0;

    while (networksCount <= 0) {
        networksCount = WiFi.scanNetworks();

        if (networksCount <= 0) {
            card_display::showWelcomeStatus(networksCount == 0
                ? "No Wi-Fi found. Retrying..." : "Wi-Fi scan failed. Retrying...");
            delay(2000);
        }
    }

    return networksCount;
}

// Shared list interaction keeps focus and scroll position within the viewport.
template <typename Draw>
int choose(int count, int& selected, Draw draw) {
    int top = std::max(0, selected - 3);
    for (;;) {
        draw(selected, top);
        const auto key = card_input::waitNavigation();
        if (key == Navigation::Select) return selected;
        if (key == Navigation::Back) return -1;
        if (key == Navigation::Up && selected > 0) --selected;
        if (key == Navigation::Down && selected + 1 < count) ++selected;
        if (selected < top) top = selected;
        if (selected >= top + 4) top = selected - 3;
    }
}

void message(const char* title, const char* body, const char* detail, bool error = false) {
    view::message(title, body, detail, error);
    while (card_input::waitNavigation() != Navigation::Select) {}
}

view::Network selectNetwork(const WifiSettings& saved) {
    for (;;) {
        const int count = scanWifiNetworks();
        std::vector<view::Network> entries;
        entries.reserve(count);
        for (int i = 0; i < count; ++i) {
            const String ssid = WiFi.SSID(i);
            entries.push_back({ssid.c_str(), WiFi.RSSI(i),
                WiFi.encryptionType(i) != WIFI_AUTH_OPEN, !ssid.isEmpty() && ssid == saved.ssid});
        }
        // The UI owns its snapshot; do not retain the driver's scan allocation.
        WiFi.scanDelete();
        int selected = 0;
        for (;;) {
            const int result = choose(count, selected, [&](int focus, int top) {
                view::networks(entries, focus, top);
            });
            if (result < 0) break;
            if (entries[result].ssid.empty()) {
                message("Hidden Wi-Fi", "This network does not broadcast a name.",
                        "Choose a visible Wi-Fi network.");
                continue;
            }
            return entries[result];
        }
    }
}

bool editField(const char* label, const char* context, String& value,
               bool optional = false, bool password = false) {
    view::field(label, context, optional, password);
    const auto result = card_input::editText(value, password, view::fieldValue);
    if (result.accepted) value = result.value;
    return result.accepted;
}

int invalidField(const char* error) {
    if (std::strstr(error, "mask")) return 1;
    if (std::strstr(error, "gateway") || std::strstr(error, "Gateway")) return 2;
    if (std::strstr(error, "primary")) return 3;
    if (std::strstr(error, "secondary")) return 4;
    return 0;
}

void editNetworkSettings(WifiSettings& settings) {
    const std::vector<view::Row> modes = {
        {"Auto (DHCP)", settings.staticIP ? "" : "Current"},
        {"Static IP", settings.staticIP ? "Current" : ""}, {"Back", ""}};
    int mode = settings.staticIP ? 1 : 0;
    const int choice = choose(3, mode, [&](int focus, int top) {
        view::menu("IP settings", "How this device gets an address", modes, focus, top);
    });
    if (choice < 0 || choice == 2) return;
    if (choice == 0) {
        settings.staticIP = false;
        return;
    }

    // Changes stay in the draft until the entire configuration validates.
    WifiSettings draft = settings;
    draft.staticIP = true;
    String* fields[] = {&draft.ip, &draft.mask, &draft.gateway, &draft.dns1, &draft.dns2};
    const char* labels[] = {"IP address", "Subnet mask", "Gateway", "Primary DNS", "Secondary DNS"};
    int selected = 0;
    for (;;) {
        std::vector<view::Row> rows;
        for (int i = 0; i < 5; ++i)
            rows.push_back({labels[i], fields[i]->isEmpty() ? (i >= 3 ? "Optional" : "Not set") : fields[i]->c_str()});
        rows.push_back({"Use settings", ""});
        rows.push_back({"Cancel", ""});
        const int action = choose(7, selected, [&](int focus, int top) {
            view::menu("Static IP", "Edit fields, then use settings", rows, focus, top);
        });
        if (action < 0 || action == 6) return;
        if (action < 5) {
            editField(labels[action], settings.ssid.c_str(), *fields[action], action >= 3);
            continue;
        }
        const char* error = wifi::validateSettings(draft);
        if (error) {
            message("Check settings", error, "Return to the highlighted field.", true);
            selected = invalidField(error);
        } else {
            settings = draft;
            return;
        }
    }
}
} // namespace

void card_wifi::setup() {
    const WifiSettings saved = wifi::loadSettings();
    for (;;) {
        const view::Network network = selectNetwork(saved);
        WifiSettings settings;
        if (network.saved) settings = saved;
        else settings.ssid = network.ssid.c_str();
        // Scan security is authoritative, even if an old saved profile had a password.
        if (!network.secure) settings.password = "";
        bool passwordSaved = network.saved && !settings.password.isEmpty();
        bool failed = false;
        int selected = 0;
        for (;;) {
            const char* passwordStatus = !network.secure ? "Not required" : settings.password.isEmpty() ?
                "Not set" : passwordSaved ? "Saved" : "Set";
            const int action = choose(4, selected, [&](int focus, int) {
                view::setup(settings.ssid.c_str(), passwordStatus, settings.staticIP, failed, focus);
            });
            if (action < 0 || action == 3) break;
            if (action == 1) {
                if (!network.secure) {
                    message("Open Wi-Fi", "No password is required.", settings.ssid.c_str());
                } else if (editField("Password", settings.ssid.c_str(), settings.password, false, true)) {
                    passwordSaved = network.saved && settings.password == saved.password;
                }
            }
            if (action == 2) editNetworkSettings(settings);
            if (action != 0) continue;
            if (network.secure && settings.password.isEmpty()) {
                if (!editField("Password", settings.ssid.c_str(), settings.password, false, true)) continue;
                passwordSaved = false;
                if (settings.password.isEmpty()) {
                    message("Password needed", "This Wi-Fi requires a password.",
                            "Choose Password to enter it.", true);
                    selected = 1;
                    continue;
                }
            }
            view::connecting(settings.ssid.c_str(), settings.staticIP, settings.ip.c_str());
            const char* error = wifi::connect(settings);
            if (!error) {
                if (!wifi::saveSettings(settings))
                    message("Connected", "Settings could not be saved.",
                            "Enter to start audio streaming.", true);
                return;
            }
            message("Can't connect", error, "Check password and IP settings, then retry.", true);
            failed = true;
            selected = 0;
        }
    }
}
