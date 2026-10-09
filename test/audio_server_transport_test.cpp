// Compile the production request handlers, not a second implementation of them.
// Fakes model HTTPD's finite session pool and deferred session closure; they do
// not emulate microphone timing, FreeRTOS scheduling or the physical network.
#include <algorithm>
#include <assert.h>
#include <map>
#include <deque>
#include <stdio.h>
#include <string>
#include <vector>
#include "../src/AudioServer.cpp"
#include "../src/WebAssets.cpp"

// Test-only equivalents of PlatformIO's embedded-resource linker symbols.
#define ASSET(name) asm(".pushsection .rodata\n.global _binary_web_" #name "_start\n" \
    "_binary_web_" #name "_start:\n.asciz \"fixture\"\n" \
    ".global _binary_web_" #name "_end\n_binary_web_" #name "_end:\n.popsection\n")
ASSET(index_html); ASSET(styles_css); ASSET(player_js); ASSET(protocol_js);
ASSET(stream_clock_js); ASSET(frame_stats_js); ASSET(pcm_player_js);
ASSET(stream_controller_js); ASSET(visualizer_js); ASSET(i18n_js); ASSET(player_view_js);
#undef ASSET

FakeCardputer M5Cardputer;
FakeSerial Serial;
FakeWifi WiFi;
unsigned fakeNotifications = 0, fakeSerialPrints = 0;
namespace {
httpd_config_t config;
std::vector<httpd_uri_t> routes;
std::map<int, unsigned> sessions;
unsigned lru = 0;
uint32_t nowMs = 100;
std::vector<int> closing;
std::map<std::string, std::string> headers;
std::string responseBody;
bool failResponse = false;
bool failClose = false;
bool failWork = false, failSend = false;
uint32_t sendCostUs = 0, clockExtraUs = 0, sendAdvanceMs = 0;
unsigned captureOnClockRead = 0;
struct Work { httpd_work_fn_t callback; void* arg; };
std::deque<Work> workQueue;
httpd_ws_type_t incomingType;
std::vector<uint8_t> incoming;
struct Sent { int fd; httpd_ws_type_t type; std::vector<uint8_t> bytes; };
std::vector<Sent> sent;

void closeSession(int fd) {
    config.close_fn(state.server, fd);
    sessions.erase(fd);
}

void flushClosures() {
    for (int fd : closing) closeSession(fd);
    closing.clear();
}

// IDF 4.4 omits the listening socket from select() when its session pool is full
// and LRU is off. A TCP connect can therefore wait forever for its HTTP upgrade.
bool acceptClient(int fd) {
    if (sessions.size() == config.max_open_sockets) {
        if (!config.lru_purge_enable) return false;
        auto oldest = std::min_element(sessions.begin(), sessions.end(),
            [](const std::pair<const int, unsigned>& a, const std::pair<const int, unsigned>& b) {
                return a.second < b.second;
            });
        closeSession(oldest->first);
    }
    sessions[fd] = ++lru;
    return config.open_fn(state.server, fd) == ESP_OK;
}

httpd_req_t request(int fd, int method = HTTP_GET) {
    httpd_req_t req;
    req.handle = state.server;
    req.fd = fd;
    req.method = method;
    return req;
}

esp_err_t message(int fd, const char* text) {
    incomingType = HTTPD_WS_TYPE_TEXT;
    incoming.assign(text, text + strlen(text));
    auto req = request(fd, 0); // IDF resets the method to zero for WS frames.
    return routes.back().handler(&req);
}
}

uint32_t millis() {
    if (captureOnClockRead && --captureOnClockRead == 0) {
        ++nowMs;
        audio_stream::Frame frame{};
        frame.sequence = 400; frame.capturedMs = nowMs;
        state.audioQueue.push(frame);
    }
    return nowMs;
}
uint32_t micros() { return nowMs * 1000 + clockExtraUs; }
esp_err_t httpd_start(httpd_handle_t* handle, const httpd_config_t* value) {
    config = *value;
    *handle = &config;
    return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t) { return ESP_OK; }
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t* route) {
    routes.push_back(*route);
    return ESP_OK;
}
esp_err_t httpd_resp_set_type(httpd_req_t*, const char* type) { headers["Content-Type"] = type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char* key, const char* value) { headers[key] = value; return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t*, const char* data, ssize_t length) {
    responseBody.assign(data, length);
    return failResponse ? ESP_FAIL : ESP_OK;
}
int httpd_req_to_sockfd(httpd_req_t* req) { return req->fd; }
esp_err_t httpd_sess_trigger_close(httpd_handle_t, int fd) {
    if (failClose) return ESP_FAIL;
    closing.push_back(fd);
    return ESP_OK;
}
esp_err_t httpd_sess_update_lru_counter(httpd_handle_t, int fd) {
    assert(sessions.count(fd));
    sessions[fd] = ++lru;
    return ESP_OK;
}
esp_err_t httpd_ws_recv_frame(httpd_req_t*, httpd_ws_frame_t* frame, size_t maxLength) {
    frame->type = incomingType;
    frame->final = true;
    frame->len = incoming.size();
    if (maxLength) {
        assert(maxLength >= incoming.size());
        std::copy(incoming.begin(), incoming.end(), frame->payload);
    }
    return ESP_OK;
}
esp_err_t httpd_ws_send_frame_async(httpd_handle_t, int fd, httpd_ws_frame_t* frame) {
    clockExtraUs += sendCostUs;
    nowMs += sendAdvanceMs;
    if (failSend) return ESP_FAIL;
    sent.push_back({fd, frame->type, {frame->payload, frame->payload + frame->len}});
    return ESP_OK;
}
esp_err_t httpd_ws_send_frame(httpd_req_t* req, httpd_ws_frame_t* frame) {
    return httpd_ws_send_frame_async(req->handle, req->fd, frame);
}
esp_err_t httpd_queue_work(httpd_handle_t, httpd_work_fn_t work, void* arg) {
    if (failWork) return ESP_FAIL;
    workQueue.push_back({work, arg});
    return ESP_OK;
}

void runWork() {
    assert(workQueue.size() == 1);
    auto work = workQueue.front(); workQueue.pop_front(); work.callback(work.arg);
}

void acknowledge(int fd, uint32_t sequence) {
    incomingType = HTTPD_WS_TYPE_BINARY;
    incoming.resize(4);
    audio_stream::writeU32(incoming.data(), sequence);
    auto req = request(fd, 0);
    assert(routes.back().handler(&req) == ESP_OK);
}

void enqueueFrames(uint32_t first, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        audio_stream::Frame frame{};
        frame.sequence = first + i; frame.capturedMs = nowMs;
        state.audioQueue.push(frame);
    }
}

int main() {
    assert(audio_server::begin() == audio_server::StartResult::Started);
    assert(routes.size() == web_assets::count() + 1);
    assert(routes.back().is_websocket && std::string(routes.back().uri) == "/audio");

    // Reproduce the old admission failure with seven abandoned keep-alive clients.
    const bool configuredLru = config.lru_purge_enable;
    config.lru_purge_enable = false;
    for (int fd = 10; fd < 17; ++fd) assert(acceptClient(fd));
    assert(!acceptClient(20));
    config.lru_purge_enable = configuredLru;
    assert(acceptClient(20)); // Regression: fails with the old server config.
    assert(!sessions.count(10));
    auto handshake = request(20);
    assert(routes.back().handler(&handshake) == ESP_OK);
    assert(message(20, "start") == ESP_OK);

    // Production handler sends a v2 frame, and transmitting refreshes its LRU.
    assert(acceptClient(21));
    audio_stream::Frame frame{};
    frame.sequence = 42;
    frame.capturedMs = nowMs;
    frame.pcm[0] = 1234;
    state.audioQueue.push(frame);
    state.queuedAudioSession.store(state.audioSession.load());
    sendAudioWork(nullptr);
    assert(sent.back().fd == 20 && sent.back().type == HTTPD_WS_TYPE_BINARY);
    assert(sent.back().bytes.size() == audio_stream::packetSize);
    assert(audio_stream::readU32(sent.back().bytes.data()) == 42);
    assert(sessions[20] > sessions[21]);
    for (int fd = 22; fd < 28; ++fd) assert(acceptClient(fd));
    assert(sessions.count(20) && state.audioListening.load());
    // Remove the deliberately abandoned clients before ordinary page reloads.
    for (int fd = 22; fd < 28; ++fd) closeSession(fd);

    // Every real asset handler sends the complete body without its NUL and then
    // queues closure, even when the browser never closes its keep-alive socket.
    for (size_t i = 0; i < web_assets::count(); ++i) {
        const int fd = 100 + int(i);
        assert(acceptClient(fd));
        auto req = request(fd);
        req.user_ctx = routes[i].user_ctx;
        headers.clear();
        assert(routes[i].handler(&req) == ESP_OK);
        assert(responseBody == "fixture");
        assert(headers["Connection"] == "close");
        assert(headers["Cache-Control"] == "no-store");
        assert(!headers["Content-Type"].empty());
        assert(sessions.count(fd));
        flushClosures();
        assert(!sessions.count(fd));
        assert(sessions.count(20) && state.audioListening.load());
    }
    // Errors propagate so HTTPD can close synchronously instead of leaking slots.
    auto req = request(200);
    req.user_ctx = routes[0].user_ctx;
    failResponse = true;
    assert(routes[0].handler(&req) == ESP_FAIL && closing.empty());
    failResponse = false;
    failClose = true;
    assert(routes[0].handler(&req) == ESP_FAIL && closing.empty());
    failClose = false;

    assert(acceptClient(201));
    assert(message(201, "start") == ESP_FAIL);
    assert(std::string(sent.back().bytes.begin(), sent.back().bytes.end()) == "busy");
    closeSession(201);
    assert(state.audioSocket == 20 && state.audioListening.load());

    // Reconnection replaces the old listener and can immediately transmit again.
    closeSession(20);
    assert(!state.audioListening.load());
    assert(acceptClient(30));
    assert(message(30, "start") == ESP_OK);
    frame.sequence = 43;
    state.audioQueue.push(frame);
    state.queuedAudioSession.store(state.audioSession.load());
    sendAudioWork(nullptr);
    assert(sent.back().fd == 30 && sent.back().bytes.size() == audio_stream::packetSize);
    // A failed publication clears pending; duplicate notifications never enqueue twice.
    assert(message(30, "sync") == ESP_OK);
    enqueueFrames(100, 10);
    failWork = true; queueAudioWork();
    assert(!state.audioWorkPending.load() && workQueue.empty());
    failWork = false; queueAudioWork(); queueAudioWork();
    assert(state.audioWorkPending.load() && workQueue.size() == 1);
    size_t before = sent.size(); runWork();
    assert(sent.size() == before + 4 && state.audioQueue.size() == 6);
    const unsigned wakes = fakeNotifications;
    acknowledge(30, 103);
    assert(fakeNotifications == wakes + 1); // Valid receipt wakes backlog promptly.
    acknowledge(30, 103);
    assert(fakeNotifications == wakes + 1); // Duplicate ACK is inert.
    // The budget is measured around the actual sends, not just a frame count.
    sendCostUs = 1100;
    queueAudioWork(); before = sent.size(); runWork();
    assert(sent.size() == before + 2 && state.audioQueue.size() == 4);
    sendCostUs = 0;

    // Deadline is refreshed between callbacks; no stale tail follows a slow send.
    assert(message(30, "sync") == ESP_OK);
    enqueueFrames(150, 4);
    nowMs += 119;
    sendAdvanceMs = 2;
    queueAudioWork(); before = sent.size(); runWork();
    assert(sent.size() == before + 1 && state.audioQueue.size() == 3);
    sendAdvanceMs = 0;
    queueAudioWork(); runWork();
    assert(sent.size() == before + 1 && state.audioQueue.stats.expired == 3);

    // A queued callback from before sync cannot cross the marker.
    queueAudioWork();
    assert(message(30, "sync") == ESP_OK);
    enqueueFrames(200, 1); before = sent.size(); runWork();
    assert(sent.size() == before && state.audioQueue.size() == 1);
    queueAudioWork(); runWork();
    assert(audio_stream::readU32(sent.back().bytes.data()) == 200);
    // Neither can work queued before close/reconnect send the new session's PCM.
    queueAudioWork(); closeSession(30);
    assert(acceptClient(30) && message(30, "start") == ESP_OK);
    enqueueFrames(300, 1); before = sent.size(); runWork();
    assert(sent.size() == before && state.audioQueue.size() == 1);
    queueAudioWork(); runWork();

    // Capture can publish between the loop's clock read and queue acquisition.
    // A clock read under the queue lock prevents unsigned age underflow/drop.
    assert(message(30, "sync") == ESP_OK);
    queueAudioWork(); before = sent.size();
    captureOnClockRead = 3; runWork();
    assert(sent.size() == before + 1 && state.audioQueue.stats.expired == 0);
    assert(audio_stream::readU32(sent.back().bytes.data()) == 400);

    // Delayed HTTPD callbacks and independent ACK return paths exercise production
    // sending. Capture stops at ten seconds; the tail and ACKs must then drain.
    struct Reply { uint32_t due, sequence; };
    for (unsigned rtt : {5u, 20u, 40u, 60u, 100u, 150u, 200u, 300u}) {
        for (unsigned service : {1u, 12u, 20u}) {
            closeSession(30);
            assert(acceptClient(30) && message(30, "start") == ESP_OK);
            std::deque<Reply> replies;
            const uint32_t start = nowMs;
            uint32_t received = 0;
            size_t cursor = sent.size();
            for (unsigned elapsed = 0; elapsed <= 10400; ++elapsed) {
                nowMs = start + elapsed;
                while (!replies.empty() && replies.front().due <= elapsed) {
                    acknowledge(30, replies.front().sequence); replies.pop_front();
                }
                if (elapsed < 10000 && elapsed % 10 == 0) enqueueFrames(elapsed / 10, 1);
                queueAudioWork();
                if (elapsed % service == 0 && !workQueue.empty()) runWork();
                while (cursor < sent.size()) {
                    const auto& packet = sent[cursor++];
                    assert(packet.type == HTTPD_WS_TYPE_BINARY);
                    const uint32_t seq = audio_stream::readU32(packet.bytes.data());
                    assert(seq == received++);
                    replies.push_back({elapsed + rtt, seq});
                }
            }
            if (!workQueue.empty()) runWork();
            assert(received == 1000 && state.metrics.sent == 1000 && replies.empty());
            assert(state.audioQueue.size() == 0);
            assert(state.audioQueue.stats.overflow == 0 && state.audioQueue.stats.expired == 0);
            assert(state.metrics.ackWaitMaxMs == rtt);
            printf("Production sender: RTT=%u ms HTTPD interval=%u ms, 1000/1000 frames\n", rtt, service);
        }
    }
    // Publishing a snapshot never writes serial on HTTPD; sender consumes it once.
    logDiagnostics();
    const unsigned prints = fakeSerialPrints;
    state.diagnosticMs = nowMs;
    publishDiagnostics(nowMs + diagnosticPeriodMs - 1);
    assert(!state.diagnostic.ready);
    publishDiagnostics(nowMs + diagnosticPeriodMs);
    assert(state.diagnostic.ready && fakeSerialPrints == prints);
    assert(state.diagnostic.capture.captured == 1000 && state.diagnostic.send.sent == 1000);
    assert(state.diagnostic.queued == 0 && state.diagnostic.send.ackWaitMaxMs == 300);
    logDiagnostics(); logDiagnostics();
    assert(fakeSerialPrints == prints + 1 && !state.diagnostic.ready);

    // A full window really pauses without clearing queued PCM. At saturation,
    // bounded capture drops old frames honestly, then a cumulative ACK releases it.
    assert(message(30, "sync") == ESP_OK);
    for (unsigned i = 0; i < audio_stream::ackCapacity; ++i) state.audioAcks.sent(i, nowMs);
    observeWindow(nowMs);
    enqueueFrames(1000, 100);
    queueAudioWork(); before = sent.size(); runWork();
    assert(sent.size() == before && state.audioQueue.size() == 12);
    assert(state.audioQueue.stats.overflow == 88);
    nowMs += 100;
    acknowledge(30, 999); // Unknown ACK must not open the window.
    assert(state.audioAcks.full());
    acknowledge(30, 31);
    assert(state.metrics.fullMs >= 100);
    queueAudioWork(); runWork();
    assert(sent.size() == before + 4);
    assert(audio_stream::readU32(sent[before].bytes.data()) == 1088);
    nowMs += 21; // Remaining captured frames are now older than 120 ms.
    queueAudioWork(); before = sent.size(); runWork();
    assert(sent.size() == before && state.audioQueue.size() == 0);
    assert(state.audioQueue.stats.expired == 8);
    enqueueFrames(1100, 1); queueAudioWork(); runWork();
    assert(audio_stream::readU32(sent.back().bytes.data()) == 1100);
    assert(message(30, "sync") == ESP_OK);
    enqueueFrames(1200, 4); failSend = true;
    queueAudioWork(); before = sent.size(); runWork();
    failSend = false;
    assert(sent.size() == before && !state.audioListening.load());
    assert(!state.audioWorkPending.load() && state.audioQueue.size() == 0);
    flushClosures();
    assert(acceptClient(30) && message(30, "start") == ESP_OK);
    enqueueFrames(1300, 1); queueAudioWork(); runWork();
    nowMs += audio_stream::ackTimeoutMs;
    queueAudioWork(); runWork();
    assert(!state.audioListening.load());
    flushClosures();
    puts("Firmware transport, bounded batching, delayed ACK, backlog and session tests passed");
}
