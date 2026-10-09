#pragma once
#include "WifiSettings.h"

namespace wifi {
// Blocks for at most 110 connection polls; nullptr means connected with an IP.
const char* connect(const WifiSettings& settings);
} // namespace wifi
