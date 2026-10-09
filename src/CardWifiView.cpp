#include "CardWifiView.h"
#include "CardTheme.h"
#include <M5Cardputer.h>
#include <algorithm>
#include <cstdio>

namespace card_wifi_view {
using namespace card_theme;
namespace { auto& screen = M5Cardputer.Display; }

void networks(const std::vector<Network>& entries, int selected, int top) {
    char badge[24];
    std::snprintf(badge, sizeof(badge), "%d/%d", selected + 1, int(entries.size()));
    header("Select Wi-Fi", badge);
    for (int i = top; i < int(entries.size()) && i < top + 4; ++i) {
        const auto& network = entries[i];
        const int y = 27 + (i - top) * 21;
        const bool active = i == selected;
        const uint16_t bg = active ? accent : surface;
        const uint16_t fg = active ? background : white;
        screen.fillRoundRect(8, y, 224, 19, 2, bg);
        text(active ? ">" : "", 12, y + 6, fg, 8, bg);
        text(network.ssid.empty() ? "(Hidden network)" : network.ssid,
             24, y + 6, fg, 151, bg);
        if (network.saved) text("*", 179, y + 6, fg, 6, bg);
        // Open shackle distinguishes open networks without using text space.
        screen.drawRoundRect(191, y + 4, 6, 7, 2, fg);
        if (!network.secure) screen.fillRect(194, y + 4, 4, 4, bg);
        screen.fillRoundRect(189, y + 9, 10, 7, 1, fg);
        const int bars = network.rssi >= -55 ? 4 : network.rssi >= -67 ? 3 :
                         network.rssi >= -78 ? 2 : 1;
        for (int bar = 0; bar < 4; ++bar)
            screen.fillRect(207 + bar * 5, y + 14 - bar * 3, 3, 3 + bar * 3,
                            bar < bars ? fg : border);
    }
    scrollbar(int(entries.size()), top, 27, 82, 4);
    footer("; / . Move   Enter Select", "Tab Rescan   * Saved");
}

void setup(const char* ssid, const char* passwordStatus, bool staticIP,
           bool failed, int selected) {
    header("Wi-Fi Setup");
    wrapped(ssid, 8, 29, 224, 2, accent);
    const Row items[] = {{failed ? "Retry connection" : "Connect", ""},
                         {"Password", passwordStatus},
                         {"IP settings", staticIP ? "Static" : "Auto (DHCP)"},
                         {"Change Wi-Fi", ""}};
    for (int i = 0; i < 4; ++i) row(items[i].label, items[i].value, 51 + i * 15, 14, selected == i);
    footer("; / . Move   Enter Select", "Tab Change Wi-Fi");
}

void menu(const char* title, const char* subtitle, const std::vector<Row>& rows,
          int selected, int top) {
    header(title);
    text(subtitle, 8, 29, muted);
    for (int i = top; i < int(rows.size()) && i < top + 4; ++i)
        row(rows[i].label, rows[i].value, 42 + (i - top) * 17, 16, selected == i);
    scrollbar(int(rows.size()), top, 42, 67, 4);
    footer("; / . Move   Enter Select", "Tab Back / Cancel");
}

void field(const char* title, const char* context, bool optional, bool password) {
    header(title);
    wrapped(context, 8, 30, 224, 2, muted);
    text(optional ? "Optional - may be left empty" :
         password ? "Enter the network password" : "Enter an IPv4 address", 8, 55, muted);
    if (!password) text(std::string(title) == "Subnet mask" ? "Example: 255.255.255.0" :
                        "Example: 192.168.1.42", 8, 99, muted);
    footer("Del Erase   Enter Apply", "Fn+Del Cancel");
}

void fieldValue(const char* value, bool visible, bool password) {
    screen.setTextFont(1);
    screen.setTextSize(1);
    inputBox();
    std::string display = password && !visible ? std::string(std::string(value).size(), '*') : value;
    bool scrolled = false;
    while (!display.empty() && screen.textWidth(display.c_str()) > 196) {
        size_t first = 1;
        while (first < display.size() && (static_cast<unsigned char>(display[first]) & 0xC0) == 0x80) ++first;
        display.erase(0, first);
        scrolled = true;
    }
    text(scrolled ? "<" : ">", 13, 79, accent, 8, surface);
    text(display, 25, 79, white, 196, surface);
    const int cursor = std::min(224, 25 + screen.textWidth(display.c_str()));
    screen.drawFastHLine(cursor, 88, 5, accent);
    if (password) {
        screen.fillRect(8, 97, 224, 12, background);
        text(visible ? "Tab Hide password" : "Tab Show password", 8, 99, muted);
    }
}

void message(const char* title, const char* body, const char* detail,
             bool error, bool acknowledge) {
    card_theme::message(title, body, detail, error,
                        acknowledge ? "Enter Continue" : "Please wait...");
}

void connecting(const char* ssid, bool staticIP, const char* ip) {
    header("Connecting...");
    wrapped(ssid, 8, 34, 224, 2, accent);
    text(staticIP ? "Static IP" : "Automatic IP (DHCP)", 8, 67, white);
    if (staticIP) text(ip, 8, 83, muted);
    footer("Joining Wi-Fi...", "Please wait (up to 11 seconds)");
}
} // namespace card_wifi_view
