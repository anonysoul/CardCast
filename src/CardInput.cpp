#include "CardInput.h"
#include <M5Cardputer.h>

namespace card_input {
bool pollAnyKeyPressed() {
    M5Cardputer.update();
    return M5Cardputer.Keyboard.isPressed();
}

// Every action waits for release, including Enter held across a page boundary.
void waitKeyboardRelease() {
    do {
        M5Cardputer.update();
        delay(pollIntervalMs);
    } while (M5Cardputer.Keyboard.isPressed());
}

Navigation waitNavigation() {
    waitKeyboardRelease();
    for (;;) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto keys = M5Cardputer.Keyboard.keysState();
            if (keys.tab) return Navigation::Back;
            if (keys.enter) return Navigation::Select;
            if (M5Cardputer.Keyboard.isKeyPressed(';')) return Navigation::Up;
            if (M5Cardputer.Keyboard.isKeyPressed('.')) return Navigation::Down;
        }
        delay(pollIntervalMs);
    }
}

TextResult editText(const String& initial, bool password, TextRenderer render) {
    waitKeyboardRelease();
    String value = initial;
    bool visible = false;
    render(value.c_str(), visible, password);
    for (;;) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            const auto keys = M5Cardputer.Keyboard.keysState();
            if (keys.fn && keys.del) return {false, initial};
            if (keys.enter) return {true, value};
            if (keys.tab) {
                if (password) {
                    visible = !visible;
                    render(value.c_str(), visible, password);
                }
                // Holding Tab must not repeatedly reveal/hide on modifier changes.
                waitKeyboardRelease();
                continue;
            }
            if (!keys.fn && !keys.ctrl && !keys.alt) {
                for (char c : keys.word) {
                    // Wi-Fi passwords fit in 64 bytes; IPv4 text in 15 bytes.
                    if (value.length() < (password ? 64U : 15U)) value += c;
                }
                if (keys.del && value.length()) {
                    unsigned int end = value.length() - 1;
                    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
                    value.remove(end);
                }
                render(value.c_str(), visible, password);
            }
        }
        delay(pollIntervalMs);
    }
}
} // namespace card_input
