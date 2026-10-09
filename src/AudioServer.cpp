#include "AudioServer.h"
#include "AudioServerStartup.h"
#include "AudioStreamState.h"
#include "WebAssets.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <atomic>
#include <errno.h>
#include <unistd.h>

namespace {
constexpr int audioCore = 1;
constexpr uint32_t httpStackSize = 6144;
constexpr uint32_t captureStackSize = 4096;
constexpr uint32_t senderStackSize = 3072;
constexpr uint32_t captureStallTimeoutMs = 1000;
constexpr uint32_t captureRetryMs = 100;
constexpr long socketSendTimeoutUs = 250000;
constexpr size_t sendBatchLimit = 4;
constexpr uint32_t sendBudgetUs = 2000;
constexpr uint32_t senderPollMs = 10;
constexpr uint32_t diagnosticPeriodMs = 5000;

// HTTPD owns the live metrics. Only a small snapshot crosses to the logging task.
struct SendMetrics {
    uint32_t sent = 0, fullMs = 0, ackWaitMaxMs = 0;
    uint32_t workWaitMaxMs = 0, sendMaxUs = 0, queueAgeMaxMs = 0;
};
struct DiagnosticSnapshot {
    SendMetrics send;
    audio_stream::FrameQueue::Stats capture;
    uint32_t session = 0;
    size_t queued = 0;
    bool ready = false;
};

// The queue and diagnostic snapshots cross task ownership under critical sections.
// Session flags and work publication cross tasks atomically; socket/ACK state
// is confined to HTTPD. Startup handles are used only by setup().
struct ServerState {
    httpd_handle_t server = nullptr;
    audio_stream::FrameQueue audioQueue;
    portMUX_TYPE audioQueueLock = portMUX_INITIALIZER_UNLOCKED;
    std::atomic<bool> audioWorkPending{false};
    std::atomic<uint32_t> audioSession{0};
    std::atomic<bool> audioListening{false};
    std::atomic<uint32_t> queuedAudioSession{0};
    // Socket and ACK state belong exclusively to the HTTP server task.
    int audioSocket = -1;
    audio_stream::AckWindow audioAcks;
    uint32_t lastAudioSentMs = 0;

    std::atomic<uint32_t> queuedAtMs{0};
    SendMetrics metrics;
    uint32_t windowObservedMs = 0, diagnosticMs = 0;
    bool windowWasFull = false;
    DiagnosticSnapshot diagnostic;
    portMUX_TYPE diagnosticLock = portMUX_INITIALIZER_UNLOCKED;

    TaskHandle_t capture = nullptr;
    TaskHandle_t sender = nullptr;
    bool started = false;
};
ServerState state;

void wakeSender() {
    if (state.sender) xTaskNotifyGive(state.sender);
}

void observeWindow(uint32_t now) {
    if (state.windowWasFull) state.metrics.fullMs += uint32_t(now - state.windowObservedMs);
    state.windowObservedMs = now;
    state.windowWasFull = state.audioAcks.full();
}

void publishDiagnostics(uint32_t now) {
    if (uint32_t(now - state.diagnosticMs) < diagnosticPeriodMs) return;
    state.diagnosticMs = now;
    DiagnosticSnapshot snapshot;
    snapshot.send = state.metrics;
    snapshot.session = state.audioSession.load();
    snapshot.ready = true;
    portENTER_CRITICAL(&state.audioQueueLock);
    snapshot.capture = state.audioQueue.stats;
    snapshot.queued = state.audioQueue.size();
    portEXIT_CRITICAL(&state.audioQueueLock);
    portENTER_CRITICAL(&state.diagnosticLock);
    state.diagnostic = snapshot;
    portEXIT_CRITICAL(&state.diagnosticLock);
}

// Serial can block; never print periodic telemetry in capture or HTTPD callbacks.
void logDiagnostics() {
    DiagnosticSnapshot snapshot;
    portENTER_CRITICAL(&state.diagnosticLock);
    snapshot = state.diagnostic;
    state.diagnostic.ready = false;
    portEXIT_CRITICAL(&state.diagnosticLock);
    if (!snapshot.ready) return;
    const auto& m = snapshot.send;
    const auto& c = snapshot.capture;
    Serial.printf("Audio stats: session=%lu captured=%lu sent=%lu queued=%u "
                  "overflow=%lu expired=%lu reset=%lu full_ms=%lu ack_wait_max_ms=%lu "
                  "work_wait_max_ms=%lu send_max_us=%lu queue_age_max_ms=%lu RSSI=%d dBm\n",
        (unsigned long)snapshot.session, (unsigned long)c.captured, (unsigned long)m.sent,
        (unsigned)snapshot.queued, (unsigned long)c.overflow, (unsigned long)c.expired,
        (unsigned long)c.reset, (unsigned long)m.fullMs, (unsigned long)m.ackWaitMaxMs,
        (unsigned long)m.workWaitMaxMs, (unsigned long)m.sendMaxUs,
        (unsigned long)m.queueAgeMaxMs, WiFi.RSSI());
}

void clearAudioQueue(bool countDrops = false) {
    portENTER_CRITICAL(&state.audioQueueLock);
    state.audioQueue.clear(countDrops);
    portEXIT_CRITICAL(&state.audioQueueLock);
}

void releaseAudioSession() {
    state.audioListening.store(false);
    state.audioSocket = -1;
    state.audioSession.fetch_add(1);
    state.audioAcks.clear();
    clearAudioQueue();
}

void recordAudioDisconnect(const char* reason) {
    Serial.printf("Audio disconnect: %s, ack_age=%lu ms, RSSI=%d dBm\n", reason,
                  (unsigned long)state.audioAcks.oldestAge(millis()), WiFi.RSSI());
}

void closeAudioConnection(const char* reason, bool notifyPeer = true) {
    const int fd = state.audioSocket;
    recordAudioDisconnect(reason);
    if (fd >= 0 && notifyPeer) {
        uint8_t payload[64] = {0x03, 0xf3}; // WebSocket 1011 + ASCII reason.
        const size_t length = strnlen(reason, sizeof(payload) - 2);
        memcpy(payload + 2, reason, length);
        httpd_ws_frame_t packet{};
        packet.type = HTTPD_WS_TYPE_CLOSE;
        packet.payload = payload;
        packet.len = length + 2;
        httpd_ws_send_frame_async(state.server, fd, &packet); // Best-effort diagnostic.
    }
    releaseAudioSession();
    if (fd >= 0) {
        // Interrupt a stalled transport immediately; HTTPD owns final close().
        shutdown(fd, SHUT_RDWR);
        httpd_sess_trigger_close(state.server, fd);
    }
}

void audioCloseSocket(httpd_handle_t, int fd) {
    if (fd == state.audioSocket) {
        recordAudioDisconnect("peer_closed");
        releaseAudioSession();
    }
    close(fd); // A custom HTTPD close callback must close the socket itself.
}

esp_err_t audioOpenSocket(httpd_handle_t, int fd) {
    const int enabled = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0
        ? ESP_OK : ESP_FAIL;
}

esp_err_t rejectAudioRequest(httpd_req_t* req, const char* reason) {
    if (httpd_req_to_sockfd(req) == state.audioSocket) {
        recordAudioDisconnect(reason);
        releaseAudioSession();
    }
    return ESP_FAIL; // HTTPD closes the failed request's session.
}

esp_err_t audioHandler(httpd_req_t* req) {
    // Claim the stream on the browser's "start" message, after the handshake.
    if (req->method == HTTP_GET) return ESP_OK;
    uint8_t payload[8]{};
    httpd_ws_frame_t packet{};
    esp_err_t result = httpd_ws_recv_frame(req, &packet, 0);
    if (result != ESP_OK) return rejectAudioRequest(req, "receive_error");
    if (!packet.final
        || !((packet.type == HTTPD_WS_TYPE_TEXT && (packet.len == 5 || packet.len == 4))
          || (packet.type == HTTPD_WS_TYPE_BINARY && packet.len == 4))) {
        return rejectAudioRequest(req, "invalid_message");
    }
    packet.payload = payload;
    if (httpd_ws_recv_frame(req, &packet, sizeof(payload)) != ESP_OK) {
        return rejectAudioRequest(req, "receive_error");
    }
    const int fd = httpd_req_to_sockfd(req);
    if (packet.type == HTTPD_WS_TYPE_TEXT && packet.len == 5
        && memcmp(payload, "start", 5) == 0) {
        if (state.audioSocket >= 0) {
            uint8_t busy[] = "busy";
            httpd_ws_frame_t response{};
            response.type = HTTPD_WS_TYPE_TEXT;
            response.payload = busy;
            response.len = 4;
            httpd_ws_send_frame(req, &response);
            return ESP_FAIL;
        }
        // Keep blocking sends bounded even when the peer stops reading.
        const timeval timeout{0, socketSendTimeoutUs};
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) return ESP_FAIL;
        state.audioSocket = fd;
        state.audioSession.fetch_add(1);
        state.audioAcks.clear();
        portENTER_CRITICAL(&state.audioQueueLock);
        state.audioQueue.clear();
        state.audioQueue.resetStats();
        portEXIT_CRITICAL(&state.audioQueueLock);
        state.metrics = SendMetrics{};
        state.windowWasFull = false;
        state.windowObservedMs = state.diagnosticMs = millis();
        state.audioListening.store(true);
        wakeSender();
        state.lastAudioSentMs = millis();
        return ESP_OK;
    }
    if (fd == state.audioSocket && packet.type == HTTPD_WS_TYPE_TEXT && packet.len == 4
        && memcmp(payload, "sync", 4) == 0) {
        // This handler and all sends run on HTTPD's task. TCP/WebSocket ordering
        // makes "synced" a barrier: all old audio precedes it, fresh audio follows.
        state.audioSession.fetch_add(1); // Invalidate work queued before the reset.
        observeWindow(millis());
        state.audioAcks.clear();
        observeWindow(millis());
        clearAudioQueue(true);
        uint8_t marker[] = "synced";
        httpd_ws_frame_t response{};
        response.type = HTTPD_WS_TYPE_TEXT;
        response.payload = marker;
        response.len = sizeof(marker) - 1;
        if (httpd_ws_send_frame(req, &response) != ESP_OK) {
            closeAudioConnection("sync_send_error", false);
        } else {
            state.lastAudioSentMs = millis();
            wakeSender();
        }
        return ESP_OK;
    }
    if (fd != state.audioSocket || packet.type != HTTPD_WS_TYPE_BINARY || packet.len != 4) {
        return rejectAudioRequest(req, "invalid_message");
    }
    // Ignore duplicate ACKs, but never let unknown ACKs open the window.
    const uint32_t now = millis();
    observeWindow(now);
    const uint32_t waitMs = state.audioAcks.oldestAge(now);
    if (state.audioAcks.acknowledge(audio_stream::readU32(payload))) {
        if (waitMs > state.metrics.ackWaitMaxMs) state.metrics.ackWaitMaxMs = waitMs;
        observeWindow(now);
        wakeSender();
    }
    return ESP_OK;
}

void sendAudioWork(void*) {
    const uint32_t startedUs = micros();
    if (state.queuedAudioSession.load() == state.audioSession.load() && state.audioSocket >= 0) {
        const uint32_t waitMs = uint32_t(millis() - state.queuedAtMs.load());
        if (waitMs > state.metrics.workWaitMaxMs) state.metrics.workWaitMaxMs = waitMs;
        for (size_t count = 0; count < sendBatchLimit; ++count) {
            const uint32_t now = millis();
            observeWindow(now);
            if (state.audioAcks.expired(now)) {
                closeAudioConnection("ack_timeout");
                break;
            }
            if (uint32_t(now - state.lastAudioSentMs) >= audio_stream::audioIdleTimeoutMs) {
                closeAudioConnection("audio_idle_timeout");
                break;
            }
            if (state.audioAcks.full()) break;
            audio_stream::Frame frame;
            portENTER_CRITICAL(&state.audioQueueLock);
            // Sample under the lock: capture may have published a newer timestamp
            // while HTTPD was preempted, which must not underflow the age check.
            const uint32_t queueNow = millis();
            const uint32_t age = state.audioQueue.oldestAge(queueNow);
            const bool available = state.audioQueue.popFresh(frame, queueNow);
            portEXIT_CRITICAL(&state.audioQueueLock);
            if (age > state.metrics.queueAgeMaxMs) state.metrics.queueAgeMaxMs = age;
            if (!available) break;
            uint8_t payload[audio_stream::packetSize];
            audio_stream::encode(frame, payload);
            httpd_ws_frame_t packet{};
            packet.type = HTTPD_WS_TYPE_BINARY;
            packet.payload = payload;
            packet.len = sizeof(payload);
            const uint32_t sendStartedUs = micros();
            const esp_err_t result = httpd_ws_send_frame_async(state.server, state.audioSocket, &packet);
            const uint32_t sendUs = uint32_t(micros() - sendStartedUs);
            if (sendUs > state.metrics.sendMaxUs) state.metrics.sendMaxUs = sendUs;
            if (result != ESP_OK) {
                Serial.printf("Audio send failed: esp_err=%d, errno=%d\n", result, errno);
                closeAudioConnection("send_error", false);
                break;
            }
            ++state.metrics.sent;
            state.audioAcks.sent(frame.sequence, millis());
            state.lastAudioSentMs = millis();
            observeWindow(state.lastAudioSentMs);
            // Outgoing audio protects the listener from idle-session LRU eviction.
            httpd_sess_update_lru_counter(state.server, state.audioSocket);
            // Cooperative budget: a single socket write still has its own timeout.
            if (uint32_t(micros() - startedUs) >= sendBudgetUs) break;
        }
        publishDiagnostics(millis());
    }
    state.audioWorkPending.store(false);
    // Drain backlog in bounded turns, allowing ACK/control requests between them.
    portENTER_CRITICAL(&state.audioQueueLock);
    const bool queued = state.audioQueue.size() != 0;
    portEXIT_CRITICAL(&state.audioQueueLock);
    if (queued && state.audioListening.load() && !state.audioAcks.full()) wakeSender();
}

void queueAudioWork() {
    if (!state.audioListening.load() || state.audioWorkPending.exchange(true)) return;
    state.queuedAudioSession.store(state.audioSession.load());
    state.queuedAtMs.store(millis());
    if (httpd_queue_work(state.server, sendAudioWork, nullptr) != ESP_OK) {
        state.audioWorkPending.store(false); // The timed wake retries a failed submission.
    }
}

void audioSendTask(void*) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
        queueAudioWork();
        logDiagnostics();
        // Capture and ACK notifications normally wake us; timeout maintains liveness.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(senderPollMs));
    }
}

void audioCaptureTask(void*) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    audio_stream::CaptureBuffers buffers;
    uint32_t sequence = 0;
    for (;;) {
        // record() queues an asynchronous operation. Only this task submits
        // buffers, so a pending count < 2 releases the oldest of our two.
        const bool ready = buffers.begin(M5Cardputer.Mic);
        uint32_t lastCompleted = millis();
        while (ready) {
            audio_stream::Frame frame;
            const auto result = buffers.poll(M5Cardputer.Mic, frame, sequence, millis());
            if (result == audio_stream::CaptureBuffers::Failed) break;
            if (result == audio_stream::CaptureBuffers::Waiting) {
                if (uint32_t(millis() - lastCompleted) > captureStallTimeoutMs) break;
                vTaskDelay(1);
                continue;
            }
            ++sequence;
            // The network owns a copy, never a buffer still used by Mic.
            portENTER_CRITICAL(&state.audioQueueLock);
            if (state.audioListening.load()) state.audioQueue.push(frame);
            portEXIT_CRITICAL(&state.audioQueueLock);
            if (state.audioListening.load()) wakeSender();
            lastCompleted = frame.capturedMs;
        }
        // end() waits for Mic's task before either buffer can be reused.
        M5Cardputer.Mic.end();
        clearAudioQueue(true);
        vTaskDelay(pdMS_TO_TICKS(captureRetryMs));
        while (!M5Cardputer.Mic.begin()) {
            Serial.println("Microphone restart failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(captureRetryMs));
        }
    }
}

// Adapter keeps ESP-IDF resource operations separate from the tested startup order.
struct Runtime {
    bool startHttp() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.max_uri_handlers = web_assets::count() + 1; // Static assets plus /audio.
        // Idle keep-alive connections must not prevent /audio from handshaking.
        config.lru_purge_enable = true;
        config.stack_size = httpStackSize;
        config.open_fn = audioOpenSocket;
        config.close_fn = audioCloseSocket;
        config.recv_wait_timeout = 1;
        config.send_wait_timeout = 1;
        return httpd_start(&state.server, &config) == ESP_OK;
    }

    bool registerRoutes() {
        if (!web_assets::registerRoutes(state.server)) return false;
        httpd_uri_t audio{};
        audio.uri = "/audio";
        audio.method = HTTP_GET;
        audio.handler = audioHandler;
        audio.is_websocket = true;
        return httpd_register_uri_handler(state.server, &audio) == ESP_OK;
    }

    bool createCapture() {
        return xTaskCreatePinnedToCore(audioCaptureTask, "Audio Capture", captureStackSize,
            nullptr, 2, &state.capture, audioCore) == pdPASS;
    }

    bool createSender() {
        return xTaskCreatePinnedToCore(audioSendTask, "Audio Sender", senderStackSize,
            nullptr, 1, &state.sender, audioCore) == pdPASS;
    }

    void deleteCapture() {
        // The task is still waiting; no microphone request owns its stack.
        vTaskDelete(state.capture);
        state.capture = nullptr;
    }

    void stopHttp() {
        httpd_stop(state.server);
        state.server = nullptr;
    }

    void activate() {
        xTaskNotifyGive(state.capture);
        xTaskNotifyGive(state.sender);
    }
};
} // namespace

namespace audio_server {
StartResult begin() {
    if (state.started) return StartResult::Started;
    Runtime runtime;
    const StartResult result = detail::start(runtime);
    state.started = result == StartResult::Started;
    if (!state.started) Serial.println(errorMessage(result));
    return result;
}

const char* errorMessage(StartResult result) {
    switch (result) {
    case StartResult::Started: return nullptr;
    case StartResult::HttpFailed: return "Audio HTTP server failed to start";
    case StartResult::RoutesFailed: return "Audio route registration failed";
    case StartResult::CaptureTaskFailed: return "Audio capture task creation failed";
    case StartResult::SenderTaskFailed: return "Audio sender task creation failed";
    }
    return "Audio startup failed";
}
} // namespace audio_server
