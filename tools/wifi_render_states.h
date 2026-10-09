#pragma once
#include "CardWifiView.h"
#include <cstring>

// Fixed, non-sensitive fixture data. All pixels are drawn by the firmware view.
inline bool renderWifiState(const char* name) {
    namespace view = card_wifi_view;
    const std::string state(name);
    const char* ssid = "CardCast Studio";
    const char* longSsid = "CardCast-Studio-Long-Network-Nam"; // 32-byte SSID.
    if (state == "select-wifi" || state == "select-wifi-scrolled") {
        const std::vector<view::Network> networks = {
            {ssid, -42, true, true}, {"Guest Wi-Fi", -61, false, false},
            {"Workshop", -73, true, false}, {longSsid, -84, true, false},
            {"Garden", -70, false, false}, {"Upstairs", -79, true, false}};
        view::networks(networks, state == "select-wifi" ? 0 : 5, state == "select-wifi" ? 0 : 2);
    } else if (state == "wifi-setup" || state == "wifi-setup-saved" ||
               state == "wifi-setup-open" || state == "wifi-setup-static" ||
               state == "wifi-setup-retry" || state == "wifi-setup-long-name" ||
               state == "wifi-setup-password-set") {
        view::setup(state == "wifi-setup-long-name" ? longSsid : ssid,
            state == "wifi-setup-saved" ? "Saved" : state == "wifi-setup-open" ? "Not required" :
            state == "wifi-setup-password-set" ? "Set" : "Not set",
            state == "wifi-setup-static", state == "wifi-setup-retry",
            state == "wifi-setup-static" ? 2 : state == "wifi-setup-password-set" ? 1 : 0);
    } else if (state == "wifi-password" || state == "wifi-password-visible" ||
               state == "wifi-password-empty" || state == "wifi-password-long") {
        view::field("Password", ssid, false, true);
        view::fieldValue(state == "wifi-password-empty" ? "" : state == "wifi-password-long" ?
            "demo-password-with-a-long-value-for-scroll-testing" : "demo-password",
            state == "wifi-password-visible", true);
    } else if (state == "wifi-ip-mode" || state == "wifi-ip-mode-static") {
        const bool isStatic = state == "wifi-ip-mode-static";
        view::menu("IP settings", "How this device gets an address",
            {{"Auto (DHCP)", isStatic ? "" : "Current"},
             {"Static IP", isStatic ? "Current" : ""}, {"Back", ""}}, isStatic ? 1 : 0);
    } else if (state == "wifi-static-fields" || state == "wifi-static-confirm" || state == "wifi-static-empty") {
        const bool empty = state == "wifi-static-empty";
        const std::vector<view::Row> fields = {
            {"IP address", empty ? "Not set" : "192.168.1.42"}, {"Subnet mask", "255.255.255.0"},
            {"Gateway", empty ? "Not set" : "192.168.1.1"}, {"Primary DNS", "Optional"},
            {"Secondary DNS", "Optional"}, {"Use settings", ""}, {"Cancel", ""}};
        view::menu("Static IP", "Edit fields, then use settings", fields,
                   state == "wifi-static-confirm" ? 5 : 0, state == "wifi-static-confirm" ? 3 : 0);
    } else if (state == "wifi-ip-edit" || state == "wifi-dns-edit" ||
               state == "wifi-mask-edit" || state == "wifi-gateway-edit" || state == "wifi-primary-dns-edit") {
        const bool dns = state == "wifi-dns-edit" || state == "wifi-primary-dns-edit";
        const char* title = state == "wifi-dns-edit" ? "Secondary DNS" :
            state == "wifi-primary-dns-edit" ? "Primary DNS" :
            state == "wifi-mask-edit" ? "Subnet mask" :
            state == "wifi-gateway-edit" ? "Gateway" : "IP address";
        view::field(title, ssid, dns, false);
        view::fieldValue(dns ? "" : state == "wifi-mask-edit" ? "255.255.255.0" :
                         state == "wifi-gateway-edit" ? "192.168.1.1" : "192.168.1.42", false, false);
    } else if (state == "wifi-static-invalid") {
        view::message("Check settings", "Gateway outside subnet", "Return to the highlighted field.", true);
    } else if (state == "wifi-connecting" || state == "wifi-connecting-static") {
        view::connecting(ssid, state == "wifi-connecting-static", "192.168.1.42");
    } else if (state == "wifi-connect-failed") {
        view::message("Can't connect", "Connection failed. Check settings.",
                      "Check password and IP settings, then retry.", true);
    } else if (state == "wifi-save-failed") {
        view::message("Connected", "Settings could not be saved.",
                      "Enter to start audio streaming.", true);
    } else if (state == "wifi-password-needed") {
        view::message("Password needed", "This Wi-Fi requires a password.",
                      "Choose Password to enter it.", true);
    } else if (state == "wifi-open-info") {
        view::message("Open Wi-Fi", "No password is required.", ssid);
    } else if (state == "wifi-hidden-info") {
        view::message("Hidden Wi-Fi", "This network does not broadcast a name.",
                      "Choose a visible Wi-Fi network.");
    } else {
        return false;
    }
    return true;
}
