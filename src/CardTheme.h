#pragma once
#include <cstdint>
#include <string>

// Shared RGB565 palette and presentation primitives for the 240 x 135 display.
namespace card_theme {
constexpr uint16_t rgb565(unsigned rgb) {
    return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}
constexpr uint16_t background = rgb565(0x121212);
constexpr uint16_t surface = rgb565(0x242424);
constexpr uint16_t border = rgb565(0x48443F);
constexpr uint16_t white = rgb565(0xF2EDE4);
constexpr uint16_t muted = rgb565(0xB6ADA1);
constexpr uint16_t accent = rgb565(0xFF8A24);
constexpr uint16_t warning = rgb565(0xF2C14E);
constexpr uint16_t error = rgb565(0xFF6961);
void text(const std::string& value, int x, int y, uint16_t color = white,
          int width = 224, uint16_t bg = background);
void header(const char* title, const char* badge = "");
void footer(const char* first, const char* second = "");
void wrapped(const std::string& value, int x, int y, int width, int lines,
             uint16_t color = white);
void scrollbar(int count, int top, int y, int height, int visible);
void row(const std::string& label, const std::string& value, int y, int height,
         bool selected, int width = 224);
void inputBox();
void message(const char* title, const char* body, const char* detail,
             bool isError, const char* hint);
} // namespace card_theme
