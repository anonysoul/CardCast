#include "../src/AudioServerStartup.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

using audio_server::StartResult;

enum class Event { Http, Routes, Capture, Sender, DeleteCapture, StopHttp, Activate };

// Model resource ownership, including the rule that unnotified workers cannot
// capture or enqueue HTTP work. Cleanup must happen before HTTPD is destroyed.
struct FakeRuntime {
    StartResult failure = StartResult::Started;
    bool http = false;
    bool routes = false;
    bool capture = false;
    bool sender = false;
    bool active = false;
    std::vector<Event> events;

    bool startHttp() {
        assert(!http && !capture && !sender && !active);
        events.push_back(Event::Http);
        http = failure != StartResult::HttpFailed;
        return http;
    }

    bool registerRoutes() {
        assert(http && !active);
        events.push_back(Event::Routes);
        routes = failure != StartResult::RoutesFailed;
        return routes;
    }

    bool createCapture() {
        assert(http && routes && !active);
        events.push_back(Event::Capture);
        capture = failure != StartResult::CaptureTaskFailed;
        return capture;
    }

    bool createSender() {
        assert(http && routes && capture && !active);
        events.push_back(Event::Sender);
        sender = failure != StartResult::SenderTaskFailed;
        return sender;
    }

    void deleteCapture() {
        assert(http && capture && !sender && !active);
        events.push_back(Event::DeleteCapture);
        capture = false;
    }

    void stopHttp() {
        assert(http && !capture && !sender && !active);
        events.push_back(Event::StopHttp);
        http = routes = false;
    }

    void activate() {
        assert(http && routes && capture && sender && !active);
        events.push_back(Event::Activate);
        active = true;
    }
};

int main() {
    const std::vector<Event> success = {
        Event::Http, Event::Routes, Event::Capture, Event::Sender, Event::Activate,
    };
    struct Case {
        StartResult failure;
        std::vector<Event> expected;
    };
    const Case failures[] = {
        {StartResult::HttpFailed, {Event::Http}},
        {StartResult::RoutesFailed, {Event::Http, Event::Routes, Event::StopHttp}},
        {StartResult::CaptureTaskFailed,
            {Event::Http, Event::Routes, Event::Capture, Event::StopHttp}},
        {StartResult::SenderTaskFailed,
            {Event::Http, Event::Routes, Event::Capture, Event::Sender,
             Event::DeleteCapture, Event::StopHttp}},
    };
    for (const auto& test : failures) {
        FakeRuntime runtime;
        runtime.failure = test.failure;
        assert(audio_server::detail::start(runtime) == test.failure);
        assert(runtime.events == test.expected);
        assert(!runtime.http && !runtime.routes && !runtime.capture && !runtime.sender);
        assert(!runtime.active);

        // A failed attempt must leave no ownership that prevents retrying.
        runtime.failure = StartResult::Started;
        runtime.events.clear();
        assert(audio_server::detail::start(runtime) == StartResult::Started);
        assert(runtime.events == success);
        assert(runtime.http && runtime.routes && runtime.capture && runtime.sender && runtime.active);
    }
    FakeRuntime runtime;
    assert(audio_server::detail::start(runtime) == StartResult::Started);
    assert(runtime.events == success);
    puts("Audio startup failure cleanup and retry tests passed");
}
