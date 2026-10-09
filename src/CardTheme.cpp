#include "CardTheme.h"
#include <M5Cardputer.h>
#include <algorithm>

namespace card_theme {
namespace { auto& screen = M5Cardputer.Display; }
// Trim only at UTF-8 boundaries; never change the underlying SSID or input.
void popCharacter(std::string& text) {
    if (text.empty()) return;
    size_t end = text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    text.resize(end);
}
std::string fit(std::string text, int width) {
    if (screen.textWidth(text.c_str()) <= width) return text;
    while (!text.empty() && screen.textWidth((text + "...").c_str()) > width)
        popCharacter(text);
    return text + "...";
}
void text(const std::string& value, int x, int y, uint16_t color,
          int width, uint16_t bg) {
    screen.setTextColor(color, bg);
    screen.drawString(fit(value, width).c_str(), x, y);
}
void header(const char* title, const char* badge) {
    screen.fillScreen(background);
    screen.setTextScroll(false);
    screen.setTextWrap(false);
    screen.setTextFont(1);
    screen.setTextSize(2);
    text(title, 8, 5, white);
    screen.setTextSize(1);
    const int width = screen.textWidth(badge);
    text(badge, 232 - width, 10, accent, width);
    screen.drawFastHLine(8, 24, 224, border);
}
void footer(const char* first, const char* second) {
    screen.fillRect(0, 114, 240, 21, background);
    screen.drawFastHLine(8, 114, 224, border);
    text(first, 8, 117, muted);
    text(second, 8, 127, muted);
}
void scrollbar(int count, int top, int y, int height, int visible) {
    if (count <= visible) return;
    const int thumb = std::max(8, height * visible / count);
    screen.fillRoundRect(236, y, 2, height, 1, surface);
    screen.fillRoundRect(236, y + (height - thumb) * top / (count - visible),
                         2, thumb, 1, accent);
}
// Character wrapping also handles long, unbroken SSIDs and diagnostic strings.
void wrapped(const std::string& value, int x, int y, int width, int lines,
             uint16_t color) {
    std::string remaining = value;
    for (int row = 0; row < lines && !remaining.empty(); ++row) {
        if (row == lines - 1) {
            text(remaining, x, y + row * 10, color, width);
            break;
        }
        size_t end = 0;
        while (end < remaining.size()) {
            size_t next = end + 1;
            while (next < remaining.size() &&
                   (static_cast<unsigned char>(remaining[next]) & 0xC0) == 0x80) ++next;
            if (screen.textWidth(remaining.substr(0, next).c_str()) > width) break;
            end = next;
        }
        if (!end) break;
        // Prefer word boundaries for prose; SSIDs without spaces still wrap safely.
        if (end < remaining.size()) {
            const size_t space = remaining.rfind(' ', end);
            if (space != std::string::npos && space > 0) end = space;
        }
        text(remaining.substr(0, end), x, y + row * 10, color, width);
        remaining.erase(0, end);
        while (!remaining.empty() && remaining.front() == ' ') remaining.erase(0, 1);
    }
}
void row(const std::string& label, const std::string& value, int y, int height, bool selected, int width) {
    const uint16_t bg = selected ? accent : surface;
    const uint16_t fg = selected ? background : white;
    screen.fillRoundRect(8, y, width, height, 2, bg);
    text(selected ? ">" : "", 12, y + (height - 8) / 2, fg, 8, bg);
    const int valueWidth = std::min(114, screen.textWidth(value.c_str()));
    text(label, 23, y + (height - 8) / 2, fg,
         width - 23 - (valueWidth ? valueWidth + 6 : 0), bg);
    if (valueWidth)
        text(value, 8 + width - 6 - valueWidth, y + (height - 8) / 2,
             selected ? background : muted, valueWidth, bg);
}

void inputBox() {
    screen.fillRoundRect(8, 71, 224, 23, 2, surface);
    screen.drawRoundRect(8, 71, 224, 23, 2, accent);
}
void message(const char* title, const char* body, const char* detail,
             bool isError, const char* hint) {
    header(title, isError ? "!" : "");
    screen.fillRoundRect(8, 32, 3, 70, 1, isError ? error : accent);
    wrapped(body, 19, 36, 210, 4, isError ? error : white);
    wrapped(detail, 19, 82, 210, 3, muted);
    footer(hint);
}
} // namespace card_theme
