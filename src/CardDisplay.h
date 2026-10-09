#pragma once
#include <stdint.h>
#include <IPAddress.h>

namespace card_display {
void initialize();
void showWelcome();
void showWelcomeStatus(const char* message);
void showStream(const IPAddress& ip);
void beginStreamIdle(uint32_t nowMs);
void updateStreamIdle(uint32_t nowMs, bool keyPressed);
void showStartupError(const char* message);
} // namespace card_display
