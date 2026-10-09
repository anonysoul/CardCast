#pragma once
#include <algorithm>
#include <vector>
struct FakeKeys {
    bool enter = false, tab = false, fn = false, del = false;
    bool ctrl = false, alt = false;
    std::vector<char> word;
};
struct FakeKeyboard {
    FakeKeys keys;
    bool changed = true;
    bool isPressed() const {
        return keys.enter || keys.tab || keys.fn || keys.del || keys.ctrl ||
               keys.alt || !keys.word.empty();
    }
    bool isChange() const { return changed; }
    FakeKeys keysState() const { return keys; }
    bool isKeyPressed(char key) const {
        return std::find(keys.word.begin(), keys.word.end(), key) != keys.word.end();
    }
};
struct FakeCardputer {
    FakeKeyboard Keyboard;
    void update();
};
extern FakeCardputer M5Cardputer;
