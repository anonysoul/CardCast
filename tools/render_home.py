#!/usr/bin/env python3
"""Render firmware home, stream and Wi-Fi screens with M5GFX. Requires c++, Pillow and pio deps.

Run from any directory: python3 tools/render_home.py
These are offscreen renders, not captures from the physical display.
"""

from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[1]
GFX = ROOT / '.pio/libdeps/m5stack-stamps3/M5GFX/src'
OUTPUT = ROOT / 'docs/screenshots'
# Render and validate every fixture, but retain only representative pages.
RETAINED_STATES = {
    'home',
    'home-no-networks',
    'select-wifi',
    'startup-microphone-failed',
    'stream',
    'wifi-connect-failed',
    'wifi-connecting',
    'wifi-ip-edit',
    'wifi-ip-mode',
    'wifi-password',
    'wifi-password-visible',
    'wifi-setup',
    'wifi-setup-static',
    'wifi-static-fields',
}

HOST = '''#pragma once
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <cassert>
using namespace lgfx;
struct HostDisplay : LGFX_Sprite {
    uint8_t brightness = 0;
    unsigned brightnessWrites = 0;
    void setBrightness(uint8_t value) {
        brightness = value;
        ++brightnessWrites;
    } // Backlight does not change framebuffer pixels.
    // Verify actual font metrics, including the bottom keyboard-hint baseline.
    size_t drawString(const char* value, int32_t x, int32_t y) {
        assert(x >= 0 && y >= 0);
        assert(x + textWidth(value) <= width());
        assert(y + fontHeight() <= height());
        return LGFX_Sprite::drawString(value, x, y);
    }
};
struct HostCardputer { HostDisplay Display; };
extern HostCardputer M5Cardputer;
'''

RENDER = r'''#include <M5Cardputer.h>
#include "CardDisplay.h"
#include "wifi_render_states.h"
#include <cstdio>
#include <cstring>
HostCardputer M5Cardputer;
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 1;
    auto& display = M5Cardputer.Display;
    display.setColorDepth(16);
    if (!display.createSprite(135, 240)) return 2;
    card_display::initialize();
    card_display::showWelcome();
    if (argc == 3 && (strcmp(argv[2], "--stream") == 0 ||
                      strcmp(argv[2], "--stream-woken") == 0)) {
        // Example address only; this renderer does not connect to Wi-Fi.
        card_display::showStream("192.168.1.42");
        if (strcmp(argv[2], "--stream-woken") == 0) {
            card_display::beginStreamIdle(0);
            card_display::updateStreamIdle(60000, false);
            assert(display.brightness == 0);
            card_display::updateStreamIdle(60020, true);
            assert(display.brightness == 80);
        }
    } else if (argc == 3 && strncmp(argv[2], "error:", 6) == 0) {
        card_display::showStartupError(argv[2] + 6);
    } else if (argc == 3 && renderWifiState(argv[2])) {
        // Production Wi-Fi drawing, using fixed simulated networks/settings.
    } else if (argc == 3) {
        display.setTextSize(1);
        if (8 + display.textWidth(argv[2]) > display.width()) return 3;
        card_display::showWelcomeStatus(argv[2]);
    }
    FILE* file = fopen(argv[1], "wb");
    if (!file) return 4;
    fprintf(file, "P6\n%d %d\n255\n", display.width(), display.height());
    for (int y = 0; y < display.height(); ++y) {
        for (int x = 0; x < display.width(); ++x) {
            auto color = display.readPixelRGB(x, y);
            unsigned char rgb[] = {color.R8(), color.G8(), color.B8()};
            fwrite(rgb, 1, 3, file);
        }
    }
    return fclose(file) == 0 ? 0 : 5;
}
'''


def main():
    if not GFX.is_dir():
        raise SystemExit('M5GFX dependency missing: run pio run first.')
    source = (ROOT / 'src/CardWifiSetup.cpp').read_text()
    # Read production scan messages so screenshots cannot silently use stale text.
    match = re.search(
        r'showWelcomeStatus\(networksCount == 0\s*\? "([^"]+)" : "([^"]+)"\)',
        source,
    )
    if not match:
        raise SystemExit('Scan status expression changed; update the renderer.')
    states = [('home', None), ('home-no-networks', match[1]),
              ('home-scan-failed', match[2]), ('stream', '--stream'),
              ('stream-woken', '--stream-woken')]
    # Extract actual diagnostics from their production call sites.
    main_source = (ROOT / 'src/main.cpp').read_text()
    microphone = re.search(r'showStartupError\("([^"\n]+)"\)', main_source)[1]
    states.append(('startup-microphone-failed', 'error:' + microphone))
    audio_source = (ROOT / 'src/AudioServer.cpp').read_text()
    for result, name in [('HttpFailed', 'http'), ('RoutesFailed', 'routes'),
                         ('CaptureTaskFailed', 'capture-task'), ('SenderTaskFailed', 'sender-task')]:
        reason = re.search(r'case StartResult::' + result + r': return "([^"]+)"', audio_source)[1]
        states.append(('startup-' + name + '-failed', 'error:' + reason))
    wifi_states = [
        'select-wifi', 'select-wifi-scrolled', 'wifi-setup', 'wifi-setup-saved',
        'wifi-setup-open', 'wifi-setup-static', 'wifi-setup-retry',
        'wifi-setup-long-name', 'wifi-setup-password-set', 'wifi-password',
        'wifi-password-visible', 'wifi-password-empty', 'wifi-password-long',
        'wifi-ip-mode', 'wifi-ip-mode-static', 'wifi-static-fields',
        'wifi-static-confirm', 'wifi-static-empty', 'wifi-ip-edit', 'wifi-dns-edit',
        'wifi-static-invalid', 'wifi-connecting', 'wifi-connecting-static',
        'wifi-connect-failed', 'wifi-save-failed', 'wifi-password-needed',
        'wifi-open-info', 'wifi-hidden-info', 'wifi-mask-edit',
        'wifi-gateway-edit', 'wifi-primary-dns-edit',
    ]
    states.extend((name, name) for name in wifi_states)
    with tempfile.TemporaryDirectory(prefix='cardputer-home-') as directory:
        build = Path(directory)
        (build / 'IPAddress.h').write_text('#pragma once\nusing IPAddress = const char*;\n')
        (build / 'M5Cardputer.h').write_text(HOST)
        (build / 'render.cpp').write_text(RENDER)
        sources = [GFX / 'lgfx/v1' / name for name in (
            'LGFXBase.cpp', 'lgfx_v1.cpp', 'lgfx_fonts.cpp', 'lgfx_v1_platforms.cpp')]
        compile_args = [
            'c++', '-std=c++17', '-O2', '-DLGFX_LINUX_FB',
            '-ffunction-sections', '-fdata-sections',
            f'-I{build}', f'-I{ROOT / "src"}', f'-I{ROOT / "tools"}', f'-I{GFX}',
            str(ROOT / 'src/CardDisplay.cpp'),
            str(ROOT / 'src/CardWifiView.cpp'), str(ROOT / 'src/CardTheme.cpp'),
            *map(str, sources), '-Wl,--gc-sections',
        ]
        subprocess.run([*compile_args, str(build / 'render.cpp'),
                        '-o', str(build / 'render')], check=True)
        subprocess.run([*compile_args, str(ROOT / 'test/card_display_test.cpp'),
                        '-o', str(build / 'display-test')], check=True)
        subprocess.run([str(build / 'display-test')], check=True)
        images = []
        for name, message in states:
            raw = build / f'{name}.ppm'
            args = [str(build / 'render'), str(raw)]
            if message:
                args.append(message)
            subprocess.run(args, check=True)
            with Image.open(raw) as image:
                image = image.copy()
            assert image.size == (240, 135)
            if name == 'stream-woken':
                original = next(img for state, img in images if state == 'stream')
                assert ImageChops.difference(original, image).getbbox() is None, \
                    'Waking changed the stream framebuffer'
            if name in ('home-no-networks', 'home-scan-failed'):
                assert ImageChops.difference(
                    images[0][1].crop((0, 0, 240, 114)),
                    image.crop((0, 0, 240, 114)),
                ).getbbox() is None, 'Status overlaps home content'
                bounds = ImageChops.difference(
                    images[0][1].crop((0, 114, 240, 135)),
                    image.crop((0, 114, 240, 135)),
                ).getbbox()
                assert bounds and bounds[2] < 240 and bounds[3] < 21
            images.append((name, image))
        OUTPUT.mkdir(parents=True, exist_ok=True)
        for name, image in images:
            if name not in RETAINED_STATES:
                continue
            image.save(OUTPUT / f'{name}.png')
            print(OUTPUT / f'{name}.png')


if __name__ == '__main__':
    main()
