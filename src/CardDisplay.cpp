#include "CardDisplay.h"
#include "CardTheme.h"
#include <M5Cardputer.h>

namespace {
constexpr uint8_t welcomeBrightness = 80;
constexpr uint32_t streamIdleTimeoutMs = 60000;
bool streamIdleEnabled = false;
bool screenAsleep = false;
uint32_t lastActivityMs = 0;

void restoreScreen() {
    streamIdleEnabled = false;
    screenAsleep = false;
    M5Cardputer.Display.setBrightness(welcomeBrightness);
}
}

namespace card_display {
using namespace card_theme;
void initialize() {
    M5Cardputer.Display.setRotation(1);
    restoreScreen();
    M5Cardputer.Display.fillScreen(background);
}

void showWelcome() {
    restoreScreen();
    header("CARDCAST", "v1.0");
    auto& display = M5Cardputer.Display;
    constexpr int heights[] = {6, 10, 16, 22, 28, 34, 40, 34, 28, 22, 16, 10, 6};
    // Static audio mark: no animation task or changes to the scan loop.
    for (int i = 0; i < 13; ++i)
        display.fillRoundRect(76 + i * 7, 67 - heights[i] / 2, 4, heights[i], 2, accent);
    text("Wi-Fi microphone", 72, 96, muted);
    footer("Scanning Wi-Fi...");
}

void showWelcomeStatus(const char* message) {
    footer(message);
}

void showStream(const IPAddress& ip) {
    restoreScreen();
    header("CardCast", "READY");
    text("Audio server ready", 8, 35, accent);
    auto& display = M5Cardputer.Display;
    display.fillRoundRect(8, 55, 224, 34, 2, surface);
    display.drawRoundRect(8, 55, 224, 34, 2, border);
    display.setTextColor(white, surface);
    display.setCursor(16, 68);
    display.print("http://");
    display.print(ip);
    display.print("/");
    footer("Open this address in your browser", "Same Wi-Fi / local network");
}

void beginStreamIdle(uint32_t nowMs) {
    restoreScreen();
    lastActivityMs = nowMs;
    streamIdleEnabled = true;
}

void updateStreamIdle(uint32_t nowMs, bool keyPressed) {
    if (!streamIdleEnabled) return;
    // A held key counts as activity; no release wait may block streaming UI polling.
    if (keyPressed) lastActivityMs = nowMs;
    // Unsigned subtraction also handles millis() wrapping during inactivity.
    const bool shouldSleep = !keyPressed && (screenAsleep ||
        static_cast<uint32_t>(nowMs - lastActivityMs) >= streamIdleTimeoutMs);
    if (shouldSleep == screenAsleep) return;
    screenAsleep = shouldSleep;
    M5Cardputer.Display.setBrightness(screenAsleep ? 0 : welcomeBrightness);
}

void showStartupError(const char* reason) {
    restoreScreen();
    card_theme::message("Startup error", reason, "Audio service unavailable", true,
                        "Restart device to retry");
}
} // namespace card_display
