#pragma once
#include "AudioServer.h"

namespace audio_server {
namespace detail {
// Workers must remain blocked until activate(). On failure, release only resources
// acquired by this attempt, before the caller can retry or report the failure.
template<class Runtime>
StartResult start(Runtime& runtime) {
    if (!runtime.startHttp()) return StartResult::HttpFailed;
    if (!runtime.registerRoutes()) {
        runtime.stopHttp();
        return StartResult::RoutesFailed;
    }
    if (!runtime.createCapture()) {
        runtime.stopHttp();
        return StartResult::CaptureTaskFailed;
    }
    if (!runtime.createSender()) {
        runtime.deleteCapture();
        runtime.stopHttp();
        return StartResult::SenderTaskFailed;
    }
    runtime.activate();
    return StartResult::Started;
}
} // namespace detail
} // namespace audio_server
