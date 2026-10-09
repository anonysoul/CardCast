#pragma once
#include <Arduino.h>

namespace card_input {
constexpr uint32_t pollIntervalMs = 20;
bool pollAnyKeyPressed();
void waitKeyboardRelease();
enum class Navigation { Up, Down, Select, Back };
Navigation waitNavigation();
struct TextResult {
    bool accepted;
    String value;
};
using TextRenderer = void (*)(const char* value, bool visible, bool password);
TextResult editText(const String& initial, bool password, TextRenderer render);
} // namespace card_input
