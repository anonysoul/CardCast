'use strict';
const assert = require('node:assert/strict');
const { test, before } = require('node:test');
let AudioEnvelope;
before(async () => {
    ({ AudioEnvelope } = await import('../web/visualizer.js'));
});
function signal(amplitude) {
    return Float32Array.from({ length: 1024 }, (_, i) => Math.sin(i * Math.PI / 20) * amplitude);
}
test('silence stays still, increasing sound increases energy, and strong sound stays bounded', () => {
    const bins = new Uint8Array(512).fill(200);
    const energies = [0, 0.002, 0.01, 0.04, 1].map(amplitude => {
        const envelope = new AudioEnvelope();
        for (let i = 0; i < 60; i++) envelope.update(signal(amplitude), bins, 48000, 1 / 30, true);
        assert([...envelope.values].every(value => value >= 0 && value <= 1));
        return envelope.values[0];
    });
    assert.equal(energies[0], 0);
    for (let i = 1; i < energies.length; i++) assert(energies[i] > energies[i - 1]);
});
test('frequency bands follow sample rate and inactive output settles even with stale analyser data', () => {
    for (const sampleRate of [16000, 44100, 48000]) {
        const envelope = new AudioEnvelope(), bins = new Uint8Array(512);
        bins[Math.round(1000 * 1024 / sampleRate)] = 255;
        for (let i = 0; i < 60; i++) envelope.update(signal(0.1), bins, sampleRate, 1 / 30, true);
        assert(envelope.values[2] > 0); assert.equal(envelope.values[1], 0); assert.equal(envelope.values[3], 0);
        for (let i = 0; i < 120; i++) envelope.update(signal(0.1), bins, sampleRate, 1 / 30, false);
        assert.deepEqual([...envelope.values], [0, 0, 0, 0]);
    }
});
test('envelope attack and release are consistent at normal and reduced-motion frame rates', () => {
    const bins = new Uint8Array(512).fill(180), samples = signal(0.1);
    const normal = new AudioEnvelope(), reduced = new AudioEnvelope();
    for (const active of [true, false]) {
        for (let i = 0; i < 30; i++) normal.update(samples, bins, 48000, 1 / 30, active);
        for (let i = 0; i < 8; i++) reduced.update(samples, bins, 48000, 1 / 8, active);
        assert(Math.abs(normal.values[0] - reduced.values[0]) < 0.00001);
    }
});
