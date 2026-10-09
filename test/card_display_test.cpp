// Compiled against production CardDisplay and M5GFX by tools/render_home.py.
#include "CardDisplay.h"
#include <M5Cardputer.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

HostCardputer M5Cardputer;

int main() {
    auto& display = M5Cardputer.Display;
    display.setColorDepth(16);
    assert(display.createSprite(135, 240));
    card_display::initialize();
    card_display::showWelcome();
    card_display::updateStreamIdle(60000, false);
    assert(display.brightness == 80);

    card_display::showStream("192.168.1.42");
    card_display::beginStreamIdle(1000);
    const auto initialWrites = display.brightnessWrites;
    card_display::updateStreamIdle(60999, false);
    assert(display.brightness == 80);
    assert(display.brightnessWrites == initialWrites);
    card_display::updateStreamIdle(61000, false);
    assert(display.brightness == 0);
    card_display::updateStreamIdle(62000, false);
    assert(display.brightnessWrites == initialWrites + 1);
    // Even after a complete timer wrap, sleeping requires a key to wake.
    card_display::updateStreamIdle(1000, false);
    assert(display.brightness == 0);
    card_display::updateStreamIdle(62020, true);
    assert(display.brightness == 80);
    assert(display.brightnessWrites == initialWrites + 2);

    // A held key and subsequent activity restart the full idle interval.
    card_display::updateStreamIdle(122020, true);
    card_display::updateStreamIdle(182019, false);
    assert(display.brightness == 80);
    card_display::updateStreamIdle(182019, true);
    card_display::updateStreamIdle(242018, false);
    assert(display.brightness == 80);
    card_display::updateStreamIdle(242019, false);
    assert(display.brightness == 0);
    card_display::updateStreamIdle(242020, true);
    assert(display.brightness == 80);

    const uint32_t nearWrap = UINT32_MAX - 30000;
    card_display::beginStreamIdle(nearWrap);
    card_display::updateStreamIdle(nearWrap + uint32_t{59999}, false);
    assert(display.brightness == 80);
    card_display::updateStreamIdle(nearWrap + uint32_t{60000}, false);
    assert(display.brightness == 0);

    card_display::showStartupError("Microphone initialization failed");
    card_display::updateStreamIdle(200000, false);
    assert(display.brightness == 80);
    card_display::beginStreamIdle(200000);
    card_display::updateStreamIdle(260000, false);
    card_display::showWelcome();
    card_display::updateStreamIdle(320000, false);
    assert(display.brightness == 80);
    std::puts("Stream idle/backlight tests passed");
}
