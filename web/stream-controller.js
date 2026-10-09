import { decodeFrame } from './protocol.js';
import { StreamClock } from './stream-clock.js';
import { PcmPlayer } from './pcm-player.js';

const CONNECTION_TIMEOUT_MS = 3000;

// Dependencies are injectable so reconnect/stop races can be tested without hardware.
export class StreamController {
    constructor({ makeContext, makeSocket, url, status, activation = () => {},
                  later = (fn, ms) => setTimeout(fn, ms), cancel = id => clearTimeout(id),
                  every = (fn, ms) => setInterval(fn, ms), cancelEvery = id => clearInterval(id),
                  now = () => performance.now() }) {
        Object.assign(this, { makeContext, makeSocket, url, status, activation, later, cancel, every, cancelEvery, now });
        this.wanted = false;
        this.visible = true;
        this.socket = null;
        this.context = null;
        this.player = null;
        this.retry = null;
        this.watchdog = null;
        this.pumpTimer = null;
        this.generation = 0;
        this.resumeAttempt = 0;
        this.recoveries = 0;
        this.reconnects = 0;
        this.lastEvent = 'None';
    }
    async start() {
        if (!this.visible && !this.context) { this.wanted = true; return; }
        if (this.wanted && this.context && this.context.state === 'running') {
            this.connect(); return;
        }
        if (!this.wanted) {
            this.recoveries = this.reconnects = 0;
            ++this.generation;
        }
        this.wanted = true;
        const generation = this.generation, attempt = ++this.resumeAttempt;
        try {
            if (!this.context) {
                const context = this.context = this.makeContext();
                this.player = new PcmPlayer(context, this.now);
                context.onstatechange = () => {
                    if (this.context !== context || !this.wanted) return;
                    if (context.state === 'running') {
                        this.activation(false);
                        this.connect();
                    } else {
                        this.disconnect();
                        if (this.visible) {
                            this.activation(true);
                            this.status('Audio suspended. Tap to resume.');
                        }
                    }
                };
            }
            // resume() may remain pending until a user gesture. Expose the
            // unlock action immediately and allow another resume on that gesture.
            if (this.context.state !== 'running') {
                this.activation(true);
                this.status('Audio suspended. Tap to resume.');
            }
            await this.context.resume();
            if (generation !== this.generation || !this.wanted || attempt !== this.resumeAttempt) return;
            if (this.context.state === 'running') {
                this.activation(false);
                this.connect();
            }
        } catch (error) {
            if (generation !== this.generation || attempt !== this.resumeAttempt) return;
            if (error.name === 'NotAllowedError') {
                this.activation(true);
                this.status('Audio suspended. Tap to resume.');
            } else {
                this.stop();
                this.activation(true);
                this.status(`Cannot start audio: ${error.message}`);
            }
        }
    }
    disconnect() {
        if (this.retry !== null) this.cancel(this.retry);
        this.retry = null;
        if (this.watchdog !== null) this.cancel(this.watchdog);
        this.watchdog = null;
        if (this.pumpTimer !== null) this.cancelEvery(this.pumpTimer);
        this.pumpTimer = null;
        const socket = this.socket;
        this.socket = null; // Invalidate callbacks before triggering close events.
        if (socket) socket.close();
        if (this.player) this.player.clear();
    }
    stop() {
        this.wanted = false;
        ++this.generation;
        this.disconnect();
        if (this.context) {
            this.context.onstatechange = null;
            this.context.close().catch(() => {});
        }
        this.activation(false);
        this.context = this.player = null;
        this.status('Stopped');
    }
    setVisible(visible) {
        this.visible = visible;
        // Visibility controls presentation, not an already-started audio session.
        // Returning may unlock a deferred start or recover browser-suspended audio.
        if (visible && this.wanted) this.start();
    }
    scheduleRetry() {
        if (!this.wanted || this.retry !== null) return;
        this.retry = this.later(() => { this.retry = null; this.connect(); }, 1000);
    }
    connect() {
        if (!this.wanted || !this.context || this.socket) return;
        if (this.context.state !== 'running') {
            this.activation(true);
            this.status('Audio suspended. Tap to resume.');
            return;
        }
        this.status('Connecting…');
        let socket;
        try { socket = this.makeSocket(this.url); }
        catch (_) { this.status('Connection failed; retrying…'); this.scheduleRetry(); return; }
        this.socket = socket;
        socket.binaryType = 'arraybuffer';
        let lastReceived = this.now();
        let syncStarted = null;
        let syncLag = 0;
        const clock = new StreamClock();
        this.pumpTimer = this.every(() => this.player && this.player.pump(), 10);
        const failed = (message) => {
            if (this.socket !== socket) return;
            this.lastEvent = message;
            ++this.reconnects;
            this.disconnect();
            this.status(message);
            this.scheduleRetry();
        };
        const checkActivity = () => {
            this.watchdog = null;
            if (this.socket !== socket) return;
            if (syncStarted !== null && this.now() - syncStarted >= CONNECTION_TIMEOUT_MS) {
                failed('Audio resync timed out; reconnecting…');
            } else if (this.now() - lastReceived >= CONNECTION_TIMEOUT_MS) {
                failed('Stream unavailable; reconnecting…');
            } else this.watchdog = this.later(checkActivity, 1000);
        };
        // Also covers a connection attempt that never completes its handshake.
        this.watchdog = this.later(checkActivity, 1000);
        socket.onopen = () => {
            if (this.socket !== socket) return;
            lastReceived = this.now();
            socket.send('start');
        };
        socket.onmessage = (event) => {
            if (this.socket !== socket) return;
            if (event.data === 'busy') { failed('Device busy; retrying…'); return; }
            if (event.data === 'synced' && syncStarted !== null) {
                // The server has flushed its queue; all pre-reset PCM precedes
                // this marker, including frames already inside TCP/browser queues.
                syncStarted = null;
                lastReceived = this.now();
                clock.reset();
                this.player.lastSequence = null;
                ++this.recoveries;
                this.lastEvent = `Discarded stale stream (${Math.round(syncLag)} ms excess delay) without reconnecting`;
                this.status('Resuming live audio…');
                return;
            }
            if (this.context.state !== 'running') {
                this.disconnect();
                this.activation(true);
                this.status('Audio suspended. Tap to resume.');
                return;
            }
            try {
                const frame = decodeFrame(event.data);
                const gap = this.now() - lastReceived;
                lastReceived = this.now();
                if (syncStarted !== null) {
                    this.player.push(frame, true);
                    return; // Do not play or ACK packets from before the marker.
                }
                const lag = clock.lag(frame.capturedMs, this.now());
                // A delivery gap alone is not evidence of stale audio. A fresh
                // frame after an outage is useful and should just refill playback.
                if ((lag > 250 && gap >= 100 && this.player.scheduledMs < 20) || lag > 500) {
                    syncStarted = this.now();
                    syncLag = lag;
                    this.player.recover();
                    this.player.push(frame, true);
                    this.lastEvent = `${Math.round(lag)} ms excess stream delay; resynchronizing`;
                    this.status('Catching up to live audio…');
                    socket.send('sync');
                    return;
                }
                const ack = new ArrayBuffer(4);
                new DataView(ack).setUint32(0, frame.sequence, true);
                socket.send(ack);
                this.player.push(frame);
                this.status(this.player.buffering ? 'Buffering…' : 'Playing');
            } catch (_) { failed('Invalid stream; reconnecting…'); }
        };
        // onclose carries the server's reason; don't destroy it in onerror.
        socket.onerror = () => {
            if (this.socket === socket) this.lastEvent = 'WebSocket transport error';
        };
        socket.onclose = (event = {}) => failed(
            `Disconnected (${event.reason || `WebSocket ${event.code || 1006}`}); retrying…`);
    }
}
