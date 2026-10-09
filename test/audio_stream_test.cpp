#include "../src/AudioStreamState.h"
#include <assert.h>
#include <deque>
#include <iostream>
using namespace audio_stream;

struct FakeMic {
    std::deque<int16_t*> pending;
    bool failing = false;
    bool record(int16_t* data, size_t count, uint32_t rate) {
        assert(count == samples && rate == sampleRate);
        if (failing) return false;
        assert(pending.size() < 2);
        for (auto ptr : pending) assert(ptr != data);
        // Simulate immediate writes, exposing premature buffer reuse in tests.
        for (size_t i = 0; i < samples; ++i) data[i] = -999;
        pending.push_back(data);
        return true;
    }
    size_t isRecording() const { return pending.size(); }
    void finish(int16_t value) {
        assert(!pending.empty());
        for (size_t i = 0; i < samples; ++i) pending.front()[i] = value;
        pending.pop_front();
    }
};

int main() {
    FakeMic mic;
    CaptureBuffers capture;
    Frame frame{};
    assert(capture.begin(mic));
    assert(capture.poll(mic, frame, 0, 0) == CaptureBuffers::Waiting);
    mic.finish(1234);
    assert(capture.poll(mic, frame, 0, 10) == CaptureBuffers::Captured);
    assert(frame.pcm[0] == 1234 && frame.pcm[159] == 1234);
    assert(capture.poll(mic, frame, 1, 11) == CaptureBuffers::Waiting);
    mic.finish(-32768);
    mic.finish(32767); // Producer wakes after both buffers have finished.
    assert(capture.poll(mic, frame, 1, 30) == CaptureBuffers::Captured);
    assert(frame.pcm[0] == -32768);
    assert(capture.poll(mic, frame, 2, 31) == CaptureBuffers::Captured);
    assert(frame.pcm[0] == 32767);
    mic.finish(42);
    mic.failing = true;
    assert(capture.poll(mic, frame, 3, 40) == CaptureBuffers::Failed);


    FrameQueue queue;
    for (uint32_t i = 0; i < 15; ++i) {
        frame.sequence = i; frame.capturedMs = i * 10; frame.pcm[0] = int16_t(i);
        queue.push(frame);
    }
    frame.pcm[0] = 100;
    assert(queue.size() == 12 && queue.stats.captured == 15 && queue.stats.overflow == 3);
    assert(queue.popFresh(frame, 140) && frame.sequence == 3 && frame.pcm[0] == 3);
    queue.clear(true);
    assert(queue.stats.reset == 11 && queue.size() == 0);
    queue.resetStats();
    frame.capturedMs = 0xfffffff0u;
    queue.push(frame);
    assert(queue.popFresh(frame, 104)); // 120 ms age across millis() rollover.
    queue.push(frame);
    assert(!queue.popFresh(frame, 105));
    assert(queue.stats.expired == 1);

    AckWindow acks;
    for (uint32_t i = 0; i < ackCapacity; ++i) acks.sent(i, 0xfffffff0u + i);
    assert(acks.full());
    assert(!acks.acknowledge(88) && acks.full());
    assert(!acks.expired(84));
    assert(!acks.expired(984));
    assert(!acks.expired(1983));
    assert(acks.expired(1984));
    assert(acks.acknowledge(10));
    assert(!acks.full() && !acks.expired(1984));
    assert(!acks.acknowledge(10));
    assert(acks.expired(1995));
    acks.clear();
    acks.sent(77, 10000);
    assert(!acks.acknowledge(11)); // A pre-sync ACK cannot free a new frame.
    assert(acks.expired(12000));
    assert(acks.acknowledge(77));
    acks.sent(0xffffffffu, 12000); acks.sent(0, 12001);
    assert(acks.acknowledge(0));
    assert(!acks.expired(15000));

    uint8_t packet[packetSize];
    frame.sequence = 0x12345678;
    frame.capturedMs = 0xabcdef01;
    frame.pcm[0] = -32768; frame.pcm[1] = 32767; frame.pcm[2] = -1;
    encode(frame, packet);
    assert(packetSize == 328 && packet[0] == 0x78 && packet[3] == 0x12);
    assert(readU32(packet) == frame.sequence && readU32(packet + 4) == frame.capturedMs);
    assert(packet[8] == 0 && packet[9] == 0x80);
    assert(packet[10] == 0xff && packet[11] == 0x7f);
    assert(packet[12] == 0xff && packet[13] == 0xff);

    // Capture continues every 10 ms regardless of whether transmission is blocked.
    struct Ack { uint32_t due, sequence; };
    for (uint32_t rtt : {5u, 20u, 40u, 60u, 100u, 150u, 200u, 300u}) {
        FrameQueue q; AckWindow window; std::deque<Ack> replies;
        uint32_t produced = 0, sent = 0;
        for (uint32_t now = 0; now < 10000; ++now) {
            while (!replies.empty() && replies.front().due <= now) {
                window.acknowledge(replies.front().sequence); replies.pop_front();
            }
            if (now % 10 == 0) {
                Frame f{}; f.sequence = produced++; f.capturedMs = now; q.push(f);
            }
            if (!window.full()) {
                Frame f{};
                if (q.popFresh(f, now)) {
                    assert(f.sequence == sent); // All samples, in original order.
                    ++sent; window.sent(f.sequence, now); replies.push_back({now + rtt, f.sequence});
                }
            }
            assert(q.size() <= captureCapacity);
        }
        assert(produced == 1000 && sent == 1000);
        assert(q.stats.overflow == 0 && q.stats.expired == 0);
        std::cout << "RTT " << rtt << " ms: " << sent << "/" << produced << " frames, no drops\n";
    }
    // Sustained blocked output must consume bounded memory, with honest counters.
    FrameQueue stalled;
    for (uint32_t i = 0; i < 1000; ++i) {
        frame.sequence = i; frame.capturedMs = i * 10; stalled.push(frame);
    }
    assert(stalled.stats.overflow == 988 && stalled.size() == 12);
    assert(stalled.popFresh(frame, 10000) && frame.sequence == 988);
    assert(!stalled.popFresh(frame, 11000));
    assert(stalled.stats.expired == 11);
    std::cout << "Audio capture, queue, ACK and wire-format tests passed\n";
}
