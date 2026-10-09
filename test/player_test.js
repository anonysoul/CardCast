'use strict';
const assert = require('node:assert/strict');
const { test, before } = require('node:test');
let decodeFrame, PcmPlayer, StreamController, StreamClock, FrameStats;
before(async () => {
    ({ decodeFrame } = await import('../web/protocol.js'));
    ({ PcmPlayer } = await import('../web/pcm-player.js'));
    ({ StreamController } = await import('../web/stream-controller.js'));
    ({ StreamClock } = await import('../web/stream-clock.js'));
    ({ FrameStats } = await import('../web/frame-stats.js'));
});
function packet(sequence, capturedMs = sequence * 10) {
    const data = new ArrayBuffer(328), view = new DataView(data);
    view.setUint32(0, sequence, true); view.setUint32(4, capturedMs, true);
    view.setInt16(8, -32768, true); view.setInt16(10, 32767, true);
    return data;
}
function context() {
    return {
        currentTime: 1, state: 'running', destination: {}, nodes: [],
        createAnalyser() { return { connect() {}, fftSize: 0, smoothingTimeConstant: 0 }; },
        createGain() { return { gain: { value: 1, setValueAtTime() {}, linearRampToValueAtTime() {} }, connect() {}, disconnect() {} }; },
        createBuffer(channels, samples, rate) {
            assert.equal(channels, 1); assert([160, 320].includes(samples)); assert.equal(rate, 16000);
            return { duration: samples / rate, copyToChannel() {} };
        },
        createBufferSource() {
            const node = { playbackRate: { value: 1 }, connect() {}, disconnect() {}, stop() { this.stopped = true; },
                start(time) { this.time = time; } };
            this.nodes.push(node); return node;
        },
        async resume() {}, async close() { this.state = 'closed'; },
    };
}
function harness() {
    const sockets = [], timers = new Map(), intervals = new Map(), contexts = [], messages = [];
    let clock = 0, timerId = 0;
    const controller = new StreamController({
        url: 'ws://device/audio',
        makeContext() { const ctx = context(); contexts.push(ctx); return ctx; },
        makeSocket() {
            const ws = { sent: [], send(data) { this.sent.push(data); }, close() { this.closed = true; } };
            sockets.push(ws); return ws;
        },
        status: message => messages.push(message),
        later(fn, ms) { assert.equal(ms, 1000); timers.set(++timerId, fn); return timerId; },
        cancel(id) { timers.delete(id); }, now: () => clock,
        every(fn, ms) { assert.equal(ms, 10); intervals.set(++timerId, fn); return timerId; },
        cancelEvery(id) { intervals.delete(id); },
    });
    return { controller, sockets, timers, intervals, contexts, messages,
        advance(ms) { clock += ms; for (const ctx of contexts) ctx.currentTime = 1 + clock / 1000; },
        retry() { const [id, fn] = timers.entries().next().value; timers.delete(id); fn(); },
    };
}
function deliver(ws, from, count, capturedMs = from * 10) {
    for (let i = 0; i < count; i++) ws.onmessage({ data: packet(from + i, capturedMs + i * 10) });
}

test('v2 PCM parsing validates size, signed samples, timestamp and sequence', () => {
    const frame = decodeFrame(packet(0xffffffff, 0xfffffffe));
    assert.equal(frame.sequence, 0xffffffff); assert.equal(frame.capturedMs, 0xfffffffe);
    assert.equal(frame.pcm[0], -1); assert.equal(frame.pcm[1], 32767 / 32768);
    assert.throws(() => decodeFrame(new ArrayBuffer(324))); assert.throws(() => decodeFrame('bad'));
});

test('four-frame bursts play every sample without clearing, gaps or drops', () => {
    const ctx = context(), player = new PcmPlayer(ctx, () => ctx.currentTime * 1000);
    let seq = 0;
    for (let ms = 0; ms < 10000; ms += 5) {
        ctx.currentTime = 1 + ms / 1000;
        if (ms % 40 === 0) for (let i = 0; i < 4; i++) player.push(decodeFrame(packet(seq++)));
        player.pump();
        assert(player.bufferedMs <= 150.001); assert(player.sources.size <= 8);
    }
    assert.equal(seq, 1000); assert.equal(player.dropped, 0); assert.equal(player.underruns, 0);
    assert(ctx.nodes.every(node => !node.stopped));
    const samples = ctx.nodes.reduce((sum, node) => sum + node.buffer.duration * 16000, 0);
    assert.equal(samples, 160000);
    for (let i = 1; i < ctx.nodes.length; i++) {
        const last = ctx.nodes[i - 1];
        assert(Math.abs(ctx.nodes[i].time - (last.time + last.buffer.duration / last.playbackRate.value)) < 1e-8);
    }
});

test('overflow trims only excess unscheduled frames and protects active audio', () => {
    const ctx = context(), player = new PcmPlayer(ctx);
    for (let i = 0; i < 6; i++) player.push(decodeFrame(packet(i)));
    ctx.currentTime += 0.010;
    const playing = [...player.sources].find(entry => entry.start <= ctx.currentTime);
    assert(playing);
    for (let i = 6; i < 30; i++) player.push(decodeFrame(packet(i)));
    assert(player.stats.trimmed > 0); assert.equal(player.stats.missing, 0);
    assert(!playing.node.stopped); assert(ctx.nodes.every(node => !node.stopped));
    assert(player.bufferedMs <= 150.001); assert(player.queue.length <= 15);
});

test('underrun is counted once, target adapts and restart waits for enough PCM', () => {
    const ctx = context(), player = new PcmPlayer(ctx);
    for (let i = 0; i < 6; i++) player.push(decodeFrame(packet(i)));
    ctx.currentTime += 0.2;
    player.pump(); player.pump();
    assert.equal(player.underruns, 1); assert.equal(player.targetMs, 70);
    for (let i = 6; i < 12; i++) player.push(decodeFrame(packet(i)));
    assert(player.buffering);
    player.push(decodeFrame(packet(12))); assert(!player.buffering);
});

test('sequence wrap, duplicates and late onended cannot inflate missing counts', () => {
    const ctx = context(), player = new PcmPlayer(ctx);
    assert(player.push(decodeFrame(packet(0xffffffff)))); assert(player.push(decodeFrame(packet(0))));
    assert(!player.push(decodeFrame(packet(0)))); assert(!player.push(decodeFrame(packet(0xffffffff))));
    assert(player.push(decodeFrame(packet(2)))); assert.equal(player.stats.missing, 1);
    for (let i = 3; i < 6; i++) player.push(decodeFrame(packet(i)));
    assert(player.sources.size > 0);
    ctx.currentTime += 0.2; player.pump();
    assert.equal(player.sources.size, 0); assert.equal(player.stats.trimmed, 0);
});

test('30-minute producer/output clock drift stays bounded without deleting audio', () => {
    for (const ppm of [-500, 500]) {
        const ctx = context(), player = new PcmPlayer(ctx, () => ctx.currentTime * 1000);
        let nextCapture = 0, sequence = 0, highWater = 0;
        for (let ms = 0; ms < 1800000; ms += 5) {
            ctx.currentTime = 1 + ms / 1000;
            while (nextCapture <= ms) {
                player.push(decodeFrame(packet(sequence++)));
                nextCapture += 10 / (1 + ppm / 1000000);
            }
            player.pump(); highWater = Math.max(highWater, player.bufferedMs);
            assert(player.sources.size <= 8 && player.queue.length <= 15);
            // The mock need not retain already inspected nodes for 30 virtual minutes.
            ctx.nodes.length = 0;
        }
        assert.equal(player.dropped, 0); assert.equal(player.underruns, 0);
        assert(highWater < 100); assert(player.rate >= 0.995 && player.rate <= 1.005);
    }
});

test('capture timestamps distinguish fresh delivery after outage from old queued audio and wrap', () => {
    const clock = new StreamClock();
    assert.equal(clock.lag(0xfffffff0, 100), 0);
    assert.equal(clock.lag(4, 120), 0);
    assert(clock.lag(14, 520) > 380);
    clock.reset(); clock.lag(100, 0);
    assert.equal(clock.lag(600, 500), 0);
});

test('recent drop rate expires and keeps missing, trim and sync categories separate', () => {
    const stats = new FrameStats();
    stats.add('received', 90, 0); stats.add('missing', 10, 0); stats.add('trimmed', 5, 0);
    assert.equal(stats.recentDropPercent(0), 15);
    assert.equal(stats.recentDropPercent(10000), 0);
    assert.equal(stats.missing, 10); assert.equal(stats.trimmed, 5); assert.equal(stats.resync, 0);
});

test('start is idempotent, ACKs precede playback and stop cancels all callbacks', async () => {
    const h = harness(), s = h.controller; await s.start(); await s.start();
    assert.equal(h.sockets.length, 1); const ws = h.sockets[0]; ws.onopen();
    deliver(ws, 0, 6);
    assert.equal(new DataView(ws.sent[1]).getUint32(0, true), 0);
    s.stop(); ws.onclose(); ws.onmessage({ data: packet(4) });
    assert.equal(h.timers.size, 0); assert.equal(h.intervals.size, 0); assert.equal(s.player, null);
});

test('fresh audio after 100–1000 ms gaps is rebuffered without dropping or syncing', async () => {
    for (const gap of [100, 200, 500, 1000]) {
        const h = harness(); await h.controller.start(); const ws = h.sockets[0];
        ws.onopen(); deliver(ws, 0, 6);
        h.advance(gap); deliver(ws, 6, 10, gap + 50);
        assert(!ws.closed); assert(!ws.sent.includes('sync'));
        assert.equal(h.controller.player.stats.trimmed, 0);
        assert.equal(h.controller.player.stats.resync, 0); assert.equal(h.controller.reconnects, 0);
        assert(h.controller.player.bufferedMs <= 150.001); h.controller.stop();
    }
});

test('old queued audio uses one sync barrier and stale frames are not acknowledged', async () => {
    const h = harness(); await h.controller.start(); const ws = h.sockets[0];
    deliver(ws, 0, 6); h.advance(400); deliver(ws, 6, 3);
    assert.equal(ws.sent.filter(value => value === 'sync').length, 1);
    assert.equal(h.controller.player.stats.resync, 3); assert(!ws.closed);
    ws.onmessage({ data: 'synced' }); h.advance(10); deliver(ws, 100, 8, 410);
    assert.equal(h.controller.recoveries, 1); assert.equal(h.controller.reconnects, 0);
    assert.equal(h.controller.player.stats.missing, 0); h.controller.stop();
});

test('busy, background, malformed data and old sockets preserve session boundaries', async () => {
    const h = harness(), s = h.controller; await s.start(); const old = h.sockets[0];
    old.onmessage({ data: 'busy' }); assert.match(h.messages.at(-1), /busy/);
    h.retry(); const current = h.sockets[1]; old.onclose(); old.onmessage({ data: 'synced' });
    assert.equal(s.socket, current); deliver(current, 0, 4);
    s.setVisible(false); assert(!current.closed); assert.equal(h.intervals.size, 1);
    s.setVisible(true); assert.equal(h.sockets.length, 2);
    current.onmessage({ data: new ArrayBuffer(1) });
    assert.match(s.lastEvent, /Invalid stream/); s.stop(); assert.equal(h.timers.size, 0);
});

test('old unscheduled PCM cannot hide exhausted audio after a main-thread stall', async () => {
    const h = harness(); await h.controller.start(); const ws = h.sockets[0];
    deliver(ws, 0, 14); assert(h.controller.player.queue.length > 0);
    h.advance(400);
    assert.equal(h.controller.player.scheduledMs, 0);
    assert(h.controller.player.bufferedMs > 20);
    deliver(ws, 14, 1);
    assert(ws.sent.includes('sync'));
    assert.equal(h.controller.player.queue.length, 0);
    h.controller.stop();
});

test('silent connections and missing sync markers eventually reconnect', async () => {
    const h = harness(); await h.controller.start(); const ws = h.sockets[0];
    deliver(ws, 0, 6); h.advance(400); deliver(ws, 6, 1);
    h.advance(1500); deliver(ws, 7, 1); h.retry(); assert(!ws.closed);
    h.advance(1500); deliver(ws, 8, 1); h.retry(); assert(ws.closed);
    assert.match(h.controller.lastEvent, /resync timed out/); h.controller.stop();
    await h.controller.start(); h.advance(3000); h.retry(); assert(h.sockets[1].closed);
    h.controller.stop(); assert.equal(h.timers.size, 0);
});

test('server reason persists after reconnect, and pending resume cannot resurrect stop', async () => {
    const h = harness(); await h.controller.start();
    h.sockets[0].onerror(); h.sockets[0].onclose({ code: 1011, reason: 'ack_timeout' });
    h.retry(); deliver(h.sockets[1], 0, 6);
    assert.equal(h.messages.at(-1), 'Playing'); assert.match(h.controller.lastEvent, /ack_timeout/);
    h.controller.stop(); let finish;
    h.controller.makeContext = () => { const ctx = context(); ctx.resume = () => new Promise(resolve => { finish = resolve; }); return ctx; };
    const starting = h.controller.start(); h.controller.stop(); finish(); await starting;
    assert.equal(h.sockets.length, 2); assert.equal(h.intervals.size, 0);
});

test('pending autoplay unlock reuses context and creates only one connection', async () => {
    const h = harness(), s = h.controller, activations = [], pending = [];
    s.activation = value => activations.push(value);
    const ctx = context(); ctx.state = 'suspended';
    ctx.resume = () => new Promise(resolve => pending.push(resolve));
    let created = 0;
    s.makeContext = () => { ++created; return ctx; };
    const autoplay = s.start(), click = s.start(), repeatedClick = s.start();
    assert.equal(h.sockets.length, 0); assert.equal(created, 1);
    assert.equal(activations.at(-1), true);
    ctx.state = 'running'; ctx.onstatechange(); pending.forEach(resolve => resolve());
    await Promise.all([autoplay, click, repeatedClick]);
    assert.equal(h.sockets.length, 1); assert.equal(activations.at(-1), false);
    assert.equal(s.player.gain.gain.value, 1); assert.equal(s.setVolume, undefined);
    s.stop();
});

test('denied autoplay and later audio interruption can be resumed with one click', async () => {
    const h = harness(), s = h.controller, activations = [];
    s.activation = value => activations.push(value);
    const ctx = context(); ctx.state = 'suspended';
    ctx.resume = async () => { const error = new Error('gesture required'); error.name = 'NotAllowedError'; throw error; };
    s.makeContext = () => ctx;
    await s.start(); assert.equal(activations.at(-1), true); assert.equal(h.sockets.length, 0);
    ctx.resume = async () => { ctx.state = 'running'; ctx.onstatechange(); };
    await s.start(); assert.equal(h.sockets.length, 1);
    ctx.state = 'interrupted'; ctx.onstatechange();
    assert(h.sockets[0].closed); assert.equal(activations.at(-1), true);
    assert.equal(h.timers.size, 0);
    await s.start(); assert.equal(h.sockets.length, 2); assert.equal(activations.at(-1), false);
    assert.equal(s.player.gain.gain.value, 1); s.stop();
});

test('stop invalidates all pending unlock attempts and late state callbacks', async () => {
    const h = harness(), s = h.controller, pending = [];
    const ctx = context(); ctx.state = 'suspended';
    ctx.resume = () => new Promise(resolve => pending.push(resolve));
    s.makeContext = () => ctx;
    const first = s.start(), second = s.start(), stateChange = ctx.onstatechange;
    s.stop(); ctx.state = 'running'; stateChange(); pending.forEach(resolve => resolve());
    await Promise.all([first, second]);
    assert.equal(h.sockets.length, 0); assert.equal(h.timers.size, 0); assert.equal(s.context, null);
});

test('loading in background defers audio and returning never restarts an explicit stop', async () => {
    const h = harness(), s = h.controller;
    s.setVisible(false); await s.start(); assert.equal(h.contexts.length, 0);
    s.setVisible(true); await Promise.resolve(); assert.equal(h.sockets.length, 1);
    s.setVisible(false); assert(!h.sockets[0].closed);
    s.setVisible(true); await Promise.resolve(); assert.equal(h.sockets.length, 1);
    s.stop(); s.setVisible(false); s.setVisible(true);
    assert.equal(h.sockets.length, 1);
});


test('background playback retains scheduling, ACKs, counters and one session', async () => {
    const h = harness(), s = h.controller;
    await s.start(); const ws = s.socket, player = s.player, ctx = s.context;
    for (let seq = 0; seq < 6000; seq += 4) {
        h.advance(40);
        s.setVisible(false);
        deliver(ws, seq, 4);
        for (const pump of h.intervals.values()) pump();
        assert(player.bufferedMs <= 150.001);
        if (seq % 400 === 0) s.setVisible(true);
    }
    assert.equal(s.socket, ws); assert(!ws.closed);
    assert.equal(s.player, player); assert.equal(s.context, ctx);
    assert.equal(h.sockets.length, 1); assert.equal(h.intervals.size, 1);
    assert.equal(ws.sent.length, 6000);
    assert.equal(player.stats.received, 6000);
    assert.equal(player.dropped, 0); assert.equal(player.underruns, 0);
    assert(ctx.nodes.length > 0); assert(ctx.nodes.every(node => !node.stopped));
    s.stop(); assert.equal(h.intervals.size, 0); assert.equal(h.timers.size, 0);
});

test('background failure retries and late retry cannot undo explicit stop', async () => {
    const h = harness(), s = h.controller;
    await s.start(); s.setVisible(false);
    h.sockets[0].onclose({ code: 1006 });
    h.retry(); const ws = h.sockets[1];
    deliver(ws, 0, 8); assert.equal(h.messages.at(-1), 'Playing');
    assert.equal(s.reconnects, 1);
    ws.onclose({ code: 1006 });
    const lateRetry = [...h.timers.values()][0];
    s.stop(); lateRetry(); s.setVisible(true);
    assert.equal(h.sockets.length, 2); assert.equal(h.timers.size, 0);
});

test('browser audio suspension in background recovers on return without duplicating a session', async () => {
    const h = harness(), s = h.controller;
    await s.start(); const ctx = s.context;
    s.setVisible(false); ctx.state = 'suspended'; ctx.onstatechange();
    assert(h.sockets[0].closed); assert.equal(h.intervals.size, 0);
    ctx.resume = async () => { ctx.state = 'running'; ctx.onstatechange(); };
    s.setVisible(true); await Promise.resolve();
    assert.equal(h.sockets.length, 2); assert.equal(s.context, ctx);
    s.setVisible(false); s.stop(); s.setVisible(true);
    assert.equal(h.sockets.length, 2);
});


test('an audio unlock completed while hidden connects without waiting for foreground', async () => {
    const h = harness(), s = h.controller, ctx = context();
    let finish;
    ctx.state = 'suspended';
    ctx.resume = () => new Promise(resolve => { finish = resolve; });
    s.makeContext = () => ctx;
    const starting = s.start();
    s.setVisible(false);
    ctx.state = 'running'; ctx.onstatechange(); finish(); await starting;
    assert.equal(h.sockets.length, 1);
    deliver(s.socket, 0, 8); assert.equal(h.messages.at(-1), 'Playing');
    s.setVisible(true); assert.equal(h.sockets.length, 1);
    s.stop();
});
