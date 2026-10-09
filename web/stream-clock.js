// Capture/receive clocks have unrelated origins. Only excess delay relative to
// the best observed offset is measured here, never absolute end-to-end latency.
export class StreamClock {
    constructor() { this.reset(); }
    reset() { this.lastCapture = null; this.captureTime = 0; this.base = null; this.lastNow = 0; }
    lag(capturedMs, now) {
        if (this.lastCapture !== null) {
            const delta = (capturedMs - this.lastCapture) >>> 0;
            if (delta >= 0x80000000) this.reset();
            else this.captureTime += delta;
        }
        this.lastCapture = capturedMs;
        const offset = now - this.captureTime;
        // Allow up to 1000 ppm clock drift without treating it as network backlog.
        this.base = this.base === null ? offset : Math.min(offset, this.base + Math.max(0, now - this.lastNow) * 0.001);
        this.lastNow = now;
        return Math.max(0, offset - this.base);
    }
}
