// Compile real configuration/input code against scripted hardware and view spies.
#include "CardWifiSetup.h"
#include "CardWifiView.h"
#include "CardInput.h"
#include "WifiSettings.h"
#include "NetworkValidation.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <cassert>
#include <deque>
#include <iostream>
#include <stdexcept>

FakeCardputer M5Cardputer;
FakeWifi WiFi;
namespace {
std::deque<FakeKeys> input;
wifi::WifiSettings saved, written;
std::vector<wifi::WifiSettings> attempts;
int failures = 0, writes = 0, retryDelays = 0;
bool saveWorks = true;
struct Page {
    std::string name, detail;
    int focus, top;
};
std::vector<Page> pages;
std::vector<std::string> inputs;
std::vector<bool> visibility;

void key(char c) {
    input.push_back({}); // Physical key release before the next action.
    FakeKeys keys;
    if (c == 'E') keys.enter = true;
    else if (c == 'B') keys.tab = true;
    else if (c == 'C') keys.fn = keys.del = true;
    else if (c == 'X') keys.del = true;
    else { assert(c == 'D' || c == 'U'); keys.word.push_back(c == 'D' ? '.' : ';'); }
    input.push_back(keys);
}
void keys(const char* sequence) { for (const char* p = sequence; *p; ++p) key(*p); }
void type(const char* text) {
    input.push_back({});
    FakeKeys keys;
    for (const char* p = text; *p; ++p) keys.word.push_back(*p);
    input.push_back(keys);
}
void reset(bool secure = false) {
    input.clear(); pages.clear(); inputs.clear(); visibility.clear(); attempts.clear();
    M5Cardputer.Keyboard = {};
    WiFi = {};
    WiFi.entries = {{"Studio", -45, secure ? 1 : WIFI_AUTH_OPEN}};
    saved = {}; written = {};
    failures = writes = retryDelays = 0;
    saveWorks = true;
}
bool seen(const char* name, int focus = -1, int top = -1) {
    for (const auto& page : pages)
        if (page.name == name && (focus < 0 || focus == page.focus) && (top < 0 || top == page.top)) return true;
    return false;
}
void run() {
    card_wifi::setup();
    assert(input.empty());
    assert(writes == 1);
    assert(WiFi.released == WiFi.scans - retryDelays);
}
void textSpy(const char* value, bool visible, bool) {
    inputs.push_back(value);
    visibility.push_back(visible);
}
} // namespace

void FakeCardputer::update() {
    if (input.empty()) throw std::runtime_error("UI requested unexpected input on " + (pages.empty() ? std::string("start") : pages.back().name));
    Keyboard.keys = input.front();
    input.pop_front();
    Keyboard.changed = true;
}
void delay(uint32_t milliseconds) { if (milliseconds == 2000) ++retryDelays; }
namespace wifi {
WifiSettings loadSettings() { return saved; }
bool saveSettings(const WifiSettings& settings) { ++writes; written = settings; return saveWorks; }
const char* validateSettings(const WifiSettings& settings) {
    if (!settings.staticIP) return nullptr;
    return validateStaticNetwork(settings.ip.c_str(), settings.mask.c_str(),
        settings.gateway.c_str(), settings.dns1.c_str(), settings.dns2.c_str());
}
const char* connect(const WifiSettings& settings) {
    assert(writes == 0); // Failed attempts must never save over the previous profile.
    attempts.push_back(settings);
    if (const char* error = validateSettings(settings)) return error;
    if (failures > 0) { --failures; return "Connection failed. Check settings."; }
    return nullptr;
}
} // namespace wifi
namespace card_display {
void showWelcome() { pages.push_back({"home", "", 0, 0}); }
void showWelcomeStatus(const char* text) { pages.push_back({text, "", 0, 0}); }
} // namespace card_display
namespace card_wifi_view {
void networks(const std::vector<Network>& networks, int focus, int top) {
    assert(focus >= top && focus < top + 4);
    pages.push_back({"networks", networks.at(focus).ssid, focus, top});
}
void setup(const char*, const char* status, bool, bool failed, int focus) {
    pages.push_back({failed ? "retry" : "setup", status, focus, 0});
}
void menu(const char* title, const char*, const std::vector<Row>& rows, int focus, int top) {
    assert(focus >= top && focus < top + 4);
    pages.push_back({title, rows.at(focus).value, focus, top});
}
void field(const char* label, const char*, bool, bool) { pages.push_back({label, "", 0, 0}); }
void fieldValue(const char* value, bool visible, bool password) { textSpy(value, visible, password); }
void message(const char* title, const char* body, const char*, bool, bool) {
    pages.push_back({title, body, 0, 0});
}
void connecting(const char*, bool, const char*) { pages.push_back({"connecting", "", 0, 0}); }
} // namespace card_wifi_view

int main() {
    // Nonblocking stream polling accepts ordinary keys and standalone modifiers.
    reset();
    std::vector<FakeKeys> wakeKeys(7);
    wakeKeys[0].word.push_back('a');
    wakeKeys[1].enter = true;
    wakeKeys[2].tab = true;
    wakeKeys[3].fn = true;
    wakeKeys[4].ctrl = true;
    wakeKeys[5].alt = true;
    wakeKeys[6].del = true;
    for (const auto& pressed : wakeKeys) {
        input.push_back({});
        input.push_back(pressed);
        input.push_back(pressed);
        assert(!card_input::pollAnyKeyPressed());
        assert(card_input::pollAnyKeyPressed());
        assert(card_input::pollAnyKeyPressed());
        assert(input.empty());
    }

    // Open networks reach the overview first, without a password prompt.
    reset(); keys("EDEEUE");
    // E select network; D/E open password info; E dismiss; U/E connect.
    run(); assert(!seen("Password")); assert(written.password.empty());

    // Selecting an encrypted network only prompts when Connect is chosen.
    reset(true); keys("EE"); type("demo-password"); key('E'); run();
    assert(seen("setup")); assert(seen("Password"));
    assert(attempts[0].password == "demo-password");

    // Cancelling a new password discards the draft, including typed characters.
    reset(true); keys("EE"); type("discard-me"); key('C'); key('E');
    type("keep-this"); key('E'); run();
    assert(attempts[0].password == "keep-this");
    assert(inputs[0].empty() && inputs[2].empty());

    // Saved credentials are restored; cancel returns to Password with original input.
    reset(true); saved.ssid = "Studio"; saved.password = "old-secret";
    keys("EDE"); type("extra"); keys("CUE"); run();
    assert(written.password == "old-secret"); assert(seen("setup", 1));
    bool restored = false;
    for (const auto& page : pages)
        if (page.name == "setup" && page.detail == "Saved") restored = true;
    assert(restored);

    // Empty confirmation explains the requirement, without trying to connect.
    reset(true); keys("EEEE"); // select, connect, empty password, acknowledge
    key('E'); type("new-secret"); keys("EUE"); run();
    assert(seen("Password needed")); assert(attempts.size() == 1);

    // Retry retains values; save failure is acknowledged before streaming.
    reset(true); saved.ssid = "Studio"; saved.password = "saved-secret";
    failures = 1; saveWorks = false; keys("EEEEE"); run();
    assert(seen("Can't connect") && seen("retry", 0) && seen("Connected"));
    assert(attempts.size() == 2 && attempts[1].password == "saved-secret");

    // List bounds, scrolling and rescan; no wrapping at either end.
    reset();
    for (int i = 0; i < 5; ++i) WiFi.entries.push_back({"Guest", -80, WIFI_AUTH_OPEN});
    keys("UDDDDDDUUBEE"); run();
    assert(seen("networks", 5, 2)); assert(seen("networks", 3, 2));
    assert(WiFi.scans == 2);

    // An unnamed scan entry cannot enter an unusable setup; choose another network.
    reset(); WiFi.entries.insert(WiFi.entries.begin(), {"", -70, 1});
    keys("EEDEE"); run();
    assert(seen("Hidden Wi-Fi") && written.ssid == "Studio");

    // An access point changed to open: never send the obsolete saved password.
    reset(); saved.ssid = "Studio"; saved.password = "old-secret";
    keys("EE"); run(); assert(written.password.empty());

    // Empty/failed scans stay on home and retry at the existing two-second interval.
    reset(); WiFi.scanResults = {0, -1}; keys("EE"); run();
    assert(retryDelays == 2);
    assert(seen("No Wi-Fi found. Retrying..."));
    assert(seen("Wi-Fi scan failed. Retrying..."));

    // Edit static fields in any order, validate, focus invalid gateway, then apply.
    reset(); keys("EDDEDE"); // overview -> IP settings -> Static IP
    key('E'); type("192.168.1.42"); keys("EDD"); // apply IP; focus Gateway
    keys("DDDE"); // Use settings with empty gateway
    key('E'); // acknowledge error; focus Gateway
    key('E'); type("192.168.1.1"); keys("EDDDEUUE"); run();
    assert(seen("Check settings")); assert(seen("Static IP", 2));
    assert(written.staticIP && written.ip == "192.168.1.42" && written.gateway == "192.168.1.1");
    assert(written.dns1.empty() && written.dns2.empty());

    // Cancelling static draft preserves DHCP and drops edits.
    reset(); keys("EDDEDEE"); type("192.168.1.99"); keys("EBUUE"); run();
    assert(!written.staticIP && written.ip.empty());

    // Cancelling an individual field retains that field's old value in the draft.
    reset(); keys("EDDEDEDE"); // Static IP -> Subnet mask editor
    key('X'); type("128"); keys("CBUUE"); run();
    assert(written.mask == "255.255.255.0" && !written.staticIP);
    std::string maskAfterCancel;
    for (const auto& page : pages)
        if (page.name == "Static IP" && page.focus == 1) maskAfterCancel = page.detail;
    assert(maskAfterCancel == "255.255.255.0");

    // Existing static config is editable and can return to DHCP without losing fields.
    reset(); saved.ssid = "Studio"; saved.staticIP = true;
    saved.ip = "192.168.1.42"; saved.gateway = "192.168.1.1";
    keys("EDDEUEUUE"); run();
    assert(!written.staticIP && written.ip == saved.ip);

    // Changing networks discards a staged password and rescans.
    reset(true); keys("EDE"); type("discard-me"); keys("EBEE");
    type("new-network-secret"); key('E'); run();
    assert(WiFi.scans == 2 && written.password == "new-network-secret");

    // A held Enter from network selection cannot submit the setup page.
    reset(); key('E');
    FakeKeys held; held.enter = true;
    input.push_back(held); input.push_back(held);
    keys("DEEUE");
    run(); assert(seen("Open Wi-Fi"));

    // Actual text loop: Tab reveal, erase, punctuation, cancel and bounded input.
    reset(); type("pa;.ss"); key('B'); keys("XE");
    auto result = card_input::editText("", true, textSpy);
    assert(result.accepted && result.value == "pa;.s");
    assert(visibility[0] == false && visibility.back() == true);
    reset(); type("changed"); key('C');
    result = card_input::editText("original", true, textSpy);
    assert(!result.accepted && result.value == "original");
    reset(); type("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"); key('E');
    result = card_input::editText("", true, textSpy);
    assert(result.value.length() == 64);
    std::cout << "Wi-Fi setup/input tests passed\n";
}
