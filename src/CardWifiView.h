#pragma once
#include <string>
#include <vector>

// Pure presentation: shared by the device and the M5GFX offscreen renderer.
namespace card_wifi_view {
struct Network {
    std::string ssid;
    int rssi;
    bool secure;
    bool saved;
};
struct Row {
    std::string label;
    std::string value;
};
void networks(const std::vector<Network>& entries, int selected, int top);
void setup(const char* ssid, const char* passwordStatus, bool staticIP,
           bool failed, int selected);
void menu(const char* title, const char* subtitle, const std::vector<Row>& rows,
          int selected, int top = 0);
void field(const char* title, const char* context, bool optional, bool password);
void fieldValue(const char* value, bool visible, bool password);
void message(const char* title, const char* body, const char* detail,
             bool error = false, bool acknowledge = true);
void connecting(const char* ssid, bool staticIP, const char* ip);
} // namespace card_wifi_view
