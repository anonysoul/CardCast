#pragma once

namespace audio_server {
enum class StartResult { Started, HttpFailed, RoutesFailed, CaptureTaskFailed, SenderTaskFailed };
// Called from setup(), once Wi-Fi and Mic are ready. Repeated successful calls are harmless.
StartResult begin();
const char* errorMessage(StartResult result);
} // namespace audio_server
