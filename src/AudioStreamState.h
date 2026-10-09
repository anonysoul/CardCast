#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace audio_stream {
constexpr size_t samples = 160;
constexpr uint32_t sampleRate = 16000;
constexpr size_t captureCapacity = 12;
// Receipt credit is independent of capture buffering: tolerate delayed ACKs.
constexpr size_t ackCapacity = 32;
constexpr uint32_t maxQueuedAgeMs = 120;
constexpr size_t packetSize = 8 + samples * sizeof(int16_t);
// Connection liveness is independent of audio queue capacity and playback delay.
constexpr uint32_t ackTimeoutMs = 2000;
constexpr uint32_t audioIdleTimeoutMs = 3000;

struct Frame {
    uint32_t sequence;
    uint32_t capturedMs;
    int16_t pcm[samples];
};

// One producer owns both requests. The microphone finishes them in FIFO order.
class CaptureBuffers {
    int16_t buffers_[2][samples]{};
    size_t oldest_ = 0;

public:
    enum Result { Waiting, Captured, Failed };
    template<class Mic>
    bool begin(Mic& mic) {
        oldest_ = 0;
        return mic.record(buffers_[0], samples, sampleRate)
            && mic.record(buffers_[1], samples, sampleRate);
    }
    template<class Mic>
    Result poll(Mic& mic, Frame& frame, uint32_t sequence, uint32_t now) {
        if (mic.isRecording() >= 2) return Waiting;
        frame.sequence = sequence;
        frame.capturedMs = now;
        memcpy(frame.pcm, buffers_[oldest_], sizeof(frame.pcm));
        // Copy before re-submitting: record() can immediately overwrite its buffer.
        if (!mic.record(buffers_[oldest_], samples, sampleRate)) return Failed;
        oldest_ ^= 1;
        return Captured;
    }
};

// Callers serialize access. Copying frames separates capture and network ownership.
class FrameQueue {
    Frame frames_[captureCapacity]{};
    size_t head_ = 0;
    size_t size_ = 0;

public:
    struct Stats { uint32_t captured = 0, overflow = 0, expired = 0, reset = 0; } stats;
    void clear(bool countDrops = false) {
        if (countDrops) stats.reset += size_;
        head_ = size_ = 0;
    }
    void resetStats() { stats = Stats{}; }
    size_t size() const { return size_; }
    uint32_t oldestAge(uint32_t now) const {
        return size_ ? uint32_t(now - frames_[head_].capturedMs) : 0;
    }
    void push(const Frame& frame) {
        ++stats.captured;
        if (size_ == captureCapacity) {
            head_ = (head_ + 1) % captureCapacity;
            --size_;
            ++stats.overflow;
        }
        frames_[(head_ + size_) % captureCapacity] = frame;
        ++size_;
    }
    bool popFresh(Frame& frame, uint32_t now) {
        while (size_) {
            frame = frames_[head_];
            head_ = (head_ + 1) % captureCapacity;
            --size_;
            if (uint32_t(now - frame.capturedMs) <= maxQueuedAgeMs) return true;
            ++stats.expired;
        }
        return false;
    }
};

class AckWindow {
    struct Entry {
        uint32_t sequence;
        uint32_t sentMs;
    };
    Entry entries_[ackCapacity]{};
    size_t size_ = 0;

public:
    void clear() { size_ = 0; }
    bool full() const { return size_ == ackCapacity; }
    uint32_t oldestAge(uint32_t now) const {
        return size_ ? uint32_t(now - entries_[0].sentMs) : 0;
    }
    bool expired(uint32_t now) const {
        return size_ && oldestAge(now) >= ackTimeoutMs;
    }
    void sent(uint32_t sequence, uint32_t now) {
        if (!full()) entries_[size_++] = {sequence, now};
    }
    bool acknowledge(uint32_t sequence) {
        for (size_t i = 0; i < size_; ++i) {
            if (entries_[i].sequence != sequence) continue;
            const size_t removed = i + 1;
            for (size_t j = removed; j < size_; ++j) entries_[j - removed] = entries_[j];
            size_ -= removed;
            return true;
        }
        return false;
    }
};

inline void writeU32(uint8_t* dst, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) dst[i] = uint8_t(value >> (8 * i));
}
inline uint32_t readU32(const uint8_t* src) {
    return uint32_t(src[0]) | (uint32_t(src[1]) << 8)
        | (uint32_t(src[2]) << 16) | (uint32_t(src[3]) << 24);
}
inline void encode(const Frame& frame, uint8_t* dst) {
    writeU32(dst, frame.sequence);
    writeU32(dst + 4, frame.capturedMs);
    for (size_t i = 0; i < samples; ++i) {
        const uint16_t value = uint16_t(frame.pcm[i]);
        dst[8 + 2 * i] = uint8_t(value);
        dst[9 + 2 * i] = uint8_t(value >> 8);
    }
}
} // namespace audio_stream
