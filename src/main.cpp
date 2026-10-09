#include "AudioServer.h"
#include "AudioStreamState.h"
#include "CardDisplay.h"
#include "CardInput.h"
#include "CardWifiSetup.h"
#include <M5Cardputer.h>
#include <WiFi.h>

namespace {
constexpr uint32_t serialBaud = 115200;
bool streamReady = false;

bool initializeMicrophone() {
    auto config = M5Cardputer.Mic.config();
    config.sample_rate = audio_stream::sampleRate;
    config.magnification = 64;
    config.noise_filter_level = 64;
    config.task_priority = 3; // Finish capture before the copy/send tasks.
    config.dma_buf_len = 64;
    config.dma_buf_count = 4;
    M5Cardputer.Mic.config(config);
    M5Cardputer.Speaker.end();
    return M5Cardputer.Mic.begin();
}
} // namespace

void setup() {
    Serial.begin(serialBaud);
    auto config = M5.config();
    M5Cardputer.begin(config);
    card_display::initialize();
    if (!initializeMicrophone()) {
        M5Cardputer.Mic.end();
        Serial.println("Microphone initialization failed");
        card_display::showStartupError("Microphone initialization failed");
        return;
    }

    card_wifi::setup();
    WiFi.setSleep(false); // Avoid power-save wakeup delays during live audio.

    const auto result = audio_server::begin();
    if (result != audio_server::StartResult::Started) {
        M5Cardputer.Mic.end();
        card_display::showStartupError(audio_server::errorMessage(result));
        return;
    }
    card_display::showStream(WiFi.localIP());
    card_display::beginStreamIdle(millis());
    streamReady = true;
}

void loop() {
    if (streamReady) {
        const bool keyPressed = card_input::pollAnyKeyPressed();
        card_display::updateStreamIdle(millis(), keyPressed);
    }
    delay(card_input::pollIntervalMs);
}
