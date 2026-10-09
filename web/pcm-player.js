import { SAMPLE_RATE, FRAME_SAMPLES, FRAME_SECONDS } from './protocol.js';
import { FrameStats } from './frame-stats.js';

const MAX_BUFFER_SECONDS = 0.150;
const LOOKAHEAD_SECONDS = 0.060;

export class PcmPlayer {
    constructor(context, now = () => performance.now()) {
        this.context = context;
        this.now = now;
        this.gain = context.createGain();
        this.gain.gain.value = 1;
        this.analyser = context.createAnalyser();
        this.analyser.fftSize = 1024;
        this.analyser.smoothingTimeConstant = 0.75;
        this.gain.connect(this.analyser);
        this.analyser.connect(context.destination);
        this.sources = new Set();
        this.queue = [];
        this.nextTime = 0;
        this.lastSequence = null;
        this.stats = new FrameStats();
        this.underruns = 0;
        this.targetMs = 60;
        this.buffering = true;
        this.fadeNext = true;
        this.level = 0.065;
        this.rate = 1;
        this.lastRateTime = context.currentTime;
        this.lastUnderrun = context.currentTime;
    }
    get bufferedMs() {
        return this.scheduledMs + this.queue.length * FRAME_SECONDS * 1000;
    }
    get scheduledMs() { return Math.max(0, this.nextTime - this.context.currentTime) * 1000; }
    get dropped() { return this.stats.missing + this.stats.trimmed + this.stats.resync; }
    prune() {
        // Count/playback state must not depend on delayed main-thread onended events.
        for (const entry of this.sources) {
            if (entry.end <= this.context.currentTime) {
                entry.node.disconnect(); entry.gain.disconnect(); this.sources.delete(entry);
            }
        }
    }
    fadeTail() {
        let tail;
        for (const entry of this.sources) if (!tail || entry.end > tail.end) tail = entry;
        if (tail && tail.end > this.context.currentTime) {
            const start = Math.max(tail.start, this.context.currentTime, tail.end - 0.003);
            tail.gain.gain.setValueAtTime(1, start);
            tail.gain.gain.linearRampToValueAtTime(0, tail.end);
        }
    }
    clear() {
        for (const entry of this.sources) { entry.node.stop(); entry.node.disconnect(); entry.gain.disconnect(); }
        this.sources.clear(); this.queue.length = 0;
        this.nextTime = 0; this.lastSequence = null; this.buffering = true; this.fadeNext = true;
        this.level = this.targetMs / 1000 + 0.005; this.rate = 1;
    }
    recover() {
        this.prune();
        let discarded = this.queue.length;
        this.queue.length = 0;
        const now = this.context.currentTime;
        this.nextTime = now;
        for (const entry of this.sources) {
            if (entry.start > now + 0.002) {
                discarded += entry.frames;
                entry.node.stop(); entry.node.disconnect(); entry.gain.disconnect(); this.sources.delete(entry);
            } else this.nextTime = Math.max(this.nextTime, entry.end);
        }
        this.stats.add('resync', discarded, this.now());
        this.fadeTail(); this.fadeNext = true; this.buffering = true;
    }
    push(frame, discard = false) {
        let discontinuity = false;
        if (this.lastSequence !== null) {
            const delta = (frame.sequence - this.lastSequence) >>> 0;
            if (delta === 0 || delta >= 0x80000000) return false;
            this.stats.add('missing', delta - 1, this.now());
            discontinuity = delta > 1;
        }
        this.lastSequence = frame.sequence;
        this.stats.add('received', 1, this.now());
        if (discard) { this.stats.add('resync', 1, this.now()); return true; }
        this.queue.push({ ...frame, discontinuity });
        // Trim only unscheduled excess. Already-playing/scheduled audio is kept.
        const excess = Math.max(0, Math.ceil((this.bufferedMs - MAX_BUFFER_SECONDS * 1000 - 1e-6) / 10));
        if (excess) {
            const removed = this.queue.splice(0, excess).length;
            this.stats.add('trimmed', removed, this.now());
            if (this.queue.length) this.queue[0].discontinuity = true;
        }
        this.pump();
        return true;
    }
    pump() {
        if (this.context.state !== 'running') return;
        const now = this.context.currentTime;
        this.prune();
        if (!this.buffering && this.nextTime < now + 0.002) {
            ++this.underruns;
            this.lastUnderrun = now;
            this.targetMs = Math.min(80, this.targetMs + 10);
            this.buffering = true; this.fadeNext = true;
        }
        if (this.buffering) {
            if (this.queue.length * 10 < this.targetMs) return;
            this.nextTime = Math.max(this.nextTime, now + 0.005);
            this.buffering = false; this.fadeNext = true;
            this.level = this.targetMs / 1000 + 0.005;
        }
        // Slowly reduce latency after a stable period. Small rate adjustments
        // absorb source/output clock drift instead of periodically deleting frames.
        if (now - this.lastUnderrun > 30 && this.targetMs > 40) {
            this.targetMs -= 10; this.lastUnderrun = now;
        }
        const elapsed = now - this.lastRateTime;
        if (elapsed >= 0.009) {
            this.level += Math.min(1, elapsed / 2) * (this.bufferedMs / 1000 - this.level);
            this.rate = Math.max(0.995, Math.min(1.005, 1 + (this.level - (this.targetMs / 1000 + 0.005)) / 4));
            this.lastRateTime = now;
        }
        while (this.queue.length && this.nextTime < now + LOOKAHEAD_SECONDS) {
            const count = this.queue.length >= 2 && !this.queue[1].discontinuity ? 2 : 1;
            const frames = this.queue.splice(0, count);
            const fade = this.fadeNext || frames[0].discontinuity;
            if (fade) this.fadeTail();
            const buffer = this.context.createBuffer(1, count * FRAME_SAMPLES, SAMPLE_RATE);
            frames.forEach((frame, index) => buffer.copyToChannel(frame.pcm, 0, index * FRAME_SAMPLES));
            const node = this.context.createBufferSource(), gain = this.context.createGain();
            node.buffer = buffer; node.playbackRate.value = this.rate;
            node.connect(gain); gain.connect(this.gain);
            const start = this.nextTime, end = start + count * FRAME_SECONDS / this.rate;
            if (fade) {
                gain.gain.setValueAtTime(0, start);
                gain.gain.linearRampToValueAtTime(1, start + 0.003);
            }
            const entry = { node, gain, start, end, frames: count };
            this.sources.add(entry);
            node.onended = () => { this.sources.delete(entry); node.disconnect(); gain.disconnect(); };
            node.start(start); this.nextTime = end; this.fadeNext = false;
        }
    }
}
