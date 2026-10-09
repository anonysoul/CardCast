'use strict';
// Optional integration smoke test: Node >= 22 and Chrome, no npm dependencies.
// Exercises the real page/Web Audio API against a simulated PCM WebSocket server.
const assert = require('node:assert/strict');
const http = require('node:http');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { once } = require('node:events');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

async function main() {
    const allowAutoplay = process.env.AUTOPLAY === 'allow';
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'cardputer-chrome-'));
    const peers = new Set(), errors = [];
    // Match the firmware's explicit asset list; never expose arbitrary files.
    const config = fs.readFileSync(path.join(__dirname, '../platformio.ini'), 'utf8');
    const assets = new Map([...config.matchAll(/^    web\/(.+)$/gm)].map(([, file]) => [
        file === 'index.html' ? '/' : `/${file}`, file,
    ]));
    const loadedAssets = new Set();
    let acknowledgements = 0, connections = 0, resyncs = 0, invalidAcks = 0, captureDrops = 0;
    let silent = false, busy = false, chrome, cdp;
    const server = http.createServer((req, res) => {
        const file = assets.get(req.url);
        if (!file) { res.writeHead(404).end(); return; }
        loadedAssets.add(req.url);
        const mime = file.endsWith('.js') ? 'application/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html';
        res.setHeader('Content-Type', `${mime}; charset=utf-8`);
        res.setHeader('Cache-Control', 'no-store');
        res.setHeader('Connection', 'close'); // Match firmware asset-session cleanup.
        res.end(fs.readFileSync(path.join(__dirname, '../web', file)));
    });
    server.on('upgrade', (req, socket) => {
        ++connections; peers.add(socket);
        const accept = crypto.createHash('sha1').update(req.headers['sec-websocket-key']
            + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
        socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
            + `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
        if (busy) {
            socket.on('error', () => {});
            socket.on('close', () => peers.delete(socket));
            socket.end(Buffer.from([0x81, 4, ...Buffer.from('busy'), 0x88, 0]));
            return;
        }
        let input = Buffer.alloc(0), started = false, sequence = 0;
        const outstanding = new Set(), ackTimers = new Set(), queue = [];
        let nextCapture = performance.now(), nextDelivery = nextCapture;
        function transmit(frame) {
            const wire = Buffer.alloc(332);
            wire[0] = 0x82; wire[1] = 126; wire.writeUInt16BE(328, 2);
            wire.writeUInt32LE(frame.sequence >>> 0, 4);
            wire.writeUInt32LE(Math.floor(frame.capturedMs) >>> 0, 8);
            for (let i = 0; i < 160; i++) {
                wire.writeInt16LE(Math.round(Math.sin((frame.sequence * 160 + i) * Math.PI / 20) * 1000), 12 + 2 * i);
            }
            outstanding.add(frame.sequence >>> 0); socket.write(wire);
        }
        socket.on('data', bytes => {
            input = Buffer.concat([input, bytes]);
            while (input.length >= 6) {
                const opcode = input[0] & 15, length = input[1] & 127;
                if (length >= 126) { socket.destroy(); return; }
                if (input.length < 6 + length) return;
                const body = Buffer.from(input.subarray(6, 6 + length));
                for (let i = 0; i < length; i++) body[i] ^= input[2 + i % 4];
                input = input.subarray(6 + length);
                if (opcode === 8) { socket.end(Buffer.from([0x88, 0])); return; }
                if (opcode === 1 && body.toString() === 'start') started = true;
                if (opcode === 1 && body.toString() === 'sync') {
                    // Model another old frame already queued before the marker.
                    if (queue.length) transmit(queue[0]);
                    queue.length = 0;
                    outstanding.clear();
                    socket.write(Buffer.concat([Buffer.from([0x81, 6]), Buffer.from('synced')]));
                    ++resyncs;
                }
                if (opcode === 2 && length === 4) {
                    const sequence = body.readUInt32LE(0);
                    if (outstanding.has(sequence)) {
                        ++acknowledgements;
                        // Fixed reverse-path delay: receiving is independent of playback.
                        const timer = setTimeout(() => { outstanding.delete(sequence); ackTimers.delete(timer); }, 60);
                        ackTimers.add(timer);
                    } else ++invalidAcks;
                }
            }
        });
        const timer = setInterval(() => {
            const now = performance.now();
            // Independent continuous capture, including while transmission stalls.
            while (nextCapture <= now) {
                const frame = { sequence: sequence++, capturedMs: nextCapture };
                nextCapture += 10;
                if (started) {
                    if (queue.length === 12) { queue.shift(); ++captureDrops; }
                    queue.push(frame);
                }
            }
            if (now < nextDelivery) return;
            while (nextDelivery <= now) nextDelivery += 40;
            if (!started || silent || socket.destroyed) return;
            while (queue.length && outstanding.size < 32) {
                const frame = queue.shift();
                if (now - frame.capturedMs > 120) { ++captureDrops; continue; }
                transmit(frame);
            }
        }, 2);
        socket.on('error', () => {});
        socket.on('close', () => {
            clearInterval(timer); for (const pending of ackTimers) clearTimeout(pending);
            peers.delete(socket);
        });
    });
    try {
        server.listen(0, '127.0.0.1'); await once(server, 'listening');
        chrome = spawn(process.env.CHROME_BIN || 'google-chrome', [
            '--headless=new', '--no-sandbox', '--disable-dev-shm-usage', '--mute-audio',
            `--autoplay-policy=${allowAutoplay ? 'no-user-gesture-required' : 'document-user-activation-required'}`,
            '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank',
        ], { stdio: 'ignore' });
        let launchError;
        chrome.on('error', error => { launchError = error; });
        const portFile = path.join(profile, 'DevToolsActivePort');
        for (let i = 0; !fs.existsSync(portFile) && i < 100; ++i) {
            if (launchError) throw launchError;
            await pause(100);
        }
        const port = fs.readFileSync(portFile, 'utf8').split('\n')[0];
        const target = await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: 'PUT' })).json();
        cdp = new WebSocket(target.webSocketDebuggerUrl);
        await new Promise((resolve, reject) => { cdp.onopen = resolve; cdp.onerror = reject; });
        let nextId = 0;
        const pending = new Map();
        cdp.onmessage = event => {
            const result = JSON.parse(event.data);
            if (result.method === 'Runtime.exceptionThrown') errors.push(result.params.exceptionDetails);
            if (pending.has(result.id)) { pending.get(result.id)(result); pending.delete(result.id); }
        };
        const command = (method, params = {}) => new Promise((resolve, reject) => {
            const id = ++nextId;
            const timeout = setTimeout(() => { pending.delete(id); reject(new Error(`${method} timed out`)); }, 5000);
            pending.set(id, result => { clearTimeout(timeout); result.error ? reject(result.error) : resolve(result.result); });
            cdp.send(JSON.stringify({ id, method, params }));
        });
        const evaluate = async (expression, userGesture = false) => {
            const result = await command('Runtime.evaluate', { expression, returnByValue: true, userGesture, awaitPromise: true });
            assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
            return result.result.value;
        };
        // Keep a small gallery; all viewport/language/interaction checks still run.
        const retainedShots = new Map(allowAutoplay
            ? [['spectrum-390.png', 'mobile'], ['spectrum-1440.png', 'pc'],
                ['advanced-zh-390.png', 'mobile'], ['advanced-en-1440.png', 'pc']]
            : [['play-mobile.png', 'mobile']]);
        async function screenshot(name, fullPage = false) {
            if (!process.env.SCREENSHOT_DIR || !retainedShots.has(name)) return;
            const directory = path.join(process.env.SCREENSHOT_DIR, retainedShots.get(name));
            fs.mkdirSync(directory, { recursive: true });
            const shot = await command('Page.captureScreenshot', { format: 'png', captureBeyondViewport: fullPage });
            fs.writeFileSync(path.join(directory, name), Buffer.from(shot.data, 'base64'));
        }
        // Exercise real UI states at both baseline viewports and in both languages.
        async function checkStates(name, focus = false) {
            for (const [width, height] of [[390, 844], [1440, 900]]) {
                await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 2, mobile: width < 800 });
                for (const lang of ['zh', 'en']) {
                    if (await evaluate('document.documentElement.lang') !== (lang === 'zh' ? 'zh-CN' : 'en'))
                        await evaluate('document.getElementById("language").click()', true);
                    if (focus) {
                        await evaluate('document.activeElement.blur()');
                        await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
                        await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 });
                        assert.equal(await evaluate('document.activeElement.matches("button:focus-visible")'), true);
                    }
                    await pause(100);
                    if (name === 'reconnecting') {
                        for (let i = 0; i < 30; i++) {
                            if (await evaluate('/busy|使用中/.test(document.getElementById("status").textContent)')) break;
                            await pause(50);
                        }
                        assert.equal(await evaluate('/busy|使用中/.test(document.getElementById("status").textContent)'), true);
                    }
                    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
                }
            }
            await evaluate('document.getElementById("language").click()', true); // Return to Chinese.
        }
        await command('Runtime.enable'); await command('Page.enable');
        await command('Page.addScriptToEvaluateOnNewDocument', { source:
            `Object.defineProperty(navigator, 'languages', { get: () => ['zh-TW', 'en-US'] });
            // Observe timer ownership without exposing application state globally.
            window.activeIntervals = new Set();
            const nativeSetInterval = window.setInterval;
            const nativeClearInterval = window.clearInterval;
            window.setInterval = (...args) => {
                const id = nativeSetInterval(...args);
                activeIntervals.add(id);
                return id;
            };
            window.clearInterval = id => {
                activeIntervals.delete(id);
                nativeClearInterval(id);
            };` });
        await command('Page.navigate', { url: `http://127.0.0.1:${server.address().port}/` });
        for (let i = 0; i < 50; ++i) {
            if (await evaluate('document.readyState === "complete" && !!document.getElementById("start")')) break;
            await pause(100);
        }
        assert.deepEqual([...loadedAssets].sort(), [...assets.keys()].sort(), 'The page must load every embedded runtime asset');
        for (const [url, file] of assets) {
            const response = await fetch(`http://127.0.0.1:${server.address().port}${url}`);
            assert.equal(response.status, 200);
            const mime = file.endsWith('.js') ? 'application/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html';
            assert.equal(response.headers.get('content-type'), `${mime}; charset=utf-8`);
            assert.equal(response.headers.get('cache-control'), 'no-store');
            assert.equal(await response.text(), fs.readFileSync(path.join(__dirname, '../web', file), 'utf8'));
        }
        assert.equal((await fetch(`http://127.0.0.1:${server.address().port}/package.json`)).status, 404);
        assert.equal(await evaluate('document.documentElement.lang'), 'zh-CN');
        assert.equal(await evaluate('document.getElementById("details").hidden'), true);
        assert.equal(await evaluate('document.querySelectorAll("input[type=range], #volume").length'), 0);
        if (!allowAutoplay) {
            await pause(200);
            assert.equal(connections, 0, 'Blocked autoplay must not occupy the only listener slot');
            assert.equal(await evaluate('document.getElementById("unlock").hidden'), false);
            await evaluate('document.getElementById("language").click()', true);
            assert.equal(await evaluate('document.querySelector("#unlock b").textContent'), 'Play');
            assert.equal(await evaluate('document.getElementById("status").textContent'), 'Audio suspended. Tap to resume.');
            await evaluate('document.getElementById("language").click()', true);
            if (process.env.SCREENSHOT_DIR) {
                await command('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 2, mobile: true });
                await pause(150);
                await screenshot('play-mobile.png');
                await command('Emulation.clearDeviceMetricsOverride');
            }
            await evaluate('document.getElementById("unlock").click()', true);
        }

        await pause(5000);
        assert.equal(await evaluate('document.getElementById("status").textContent'), '正在播放');
        assert(acknowledgements >= 450, 'Browser must sustain continuous PCM despite 60 ms delayed ACKs');
        assert.equal(captureDrops, 0);
        assert.equal(Number(await evaluate('document.getElementById("dropped").textContent')), 0);
        assert.equal(Number(await evaluate('document.getElementById("underruns").textContent')), 0);
        assert(Number(await evaluate('document.getElementById("buffer").textContent')) <= 151);
        assert.equal(await evaluate('document.getElementById("unlock").hidden'), true);
        assert.equal(connections, 1);
        for (const [width, height] of [[360, 800], [390, 844], [768, 1024], [1440, 900], [844, 390]]) {
            await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 2, mobile: width < 800 });
            await pause(150);
            assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
            assert.equal(await evaluate('document.documentElement.scrollHeight <= innerHeight + 1'), true);
            const extent = await evaluate(`(() => {
                const canvas = document.getElementById('spectrum');
                const bytes = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
                let min = canvas.height, max = -1;
                for (let y = 0; y < canvas.height; y++) for (let x = 0; x < canvas.width; x++) {
                    if (bytes[(y * canvas.width + x) * 4 + 3] > 20) { min = Math.min(min, y); max = Math.max(max, y); }
                }
                return { min, max, height: canvas.height };
            })()`);
            assert(extent.max - extent.min > 8, 'Live sound must open the ribbons beyond the idle line');
            assert(extent.min > 0 && extent.max < extent.height - 1, 'Ribbons must stay within the canvas');
            await screenshot(`spectrum-${width}.png`);
            await evaluate('document.getElementById("advanced").click()', true);
            assert.equal(await evaluate('document.getElementById("details").hidden'), false);
            assert.equal(await evaluate('document.getElementById("advanced").getAttribute("aria-expanded")'), 'true');
            assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
            await pause(200);
            const beforeLanguage = connections;
            const recoveryCount = await evaluate('document.getElementById("recoveries").textContent');
            assert.equal(await evaluate('document.title'), 'CardCast · 实时音频');
            await screenshot(`advanced-zh-${width}.png`, true);
            await evaluate('document.getElementById("language").click()', true);
            assert.equal(await evaluate('document.documentElement.lang'), 'en');
            assert.equal(await evaluate('document.title'), 'CardCast · Live Audio');
            assert.equal(await evaluate('document.getElementById("advanced").textContent'), 'Collapse');
            assert.equal(await evaluate('document.getElementById("details").hidden'), false);
            assert.equal(await evaluate('document.getElementById("status").textContent'), 'Playing');
            assert.equal(await evaluate('document.getElementById("recoveries").textContent'), recoveryCount);
            assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true);
            assert.equal(await evaluate(`(() => {
                const a = document.getElementById('language').getBoundingClientRect();
                const b = document.getElementById('advanced').getBoundingClientRect();
                return a.right <= b.left && a.left >= 0 && b.right <= innerWidth && a.height >= 44 && b.height >= 44;
            })()`), true);
            await pause(100);
            assert.equal(connections, beforeLanguage, 'Language changes must not reconnect');
            await screenshot(`advanced-en-${width}.png`, true);
            await evaluate('document.getElementById("language").click()', true);
            await evaluate('document.getElementById("advanced").click()', true);
        }
        await command('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });
        await pause(300);
        assert.equal(await evaluate('matchMedia("(prefers-reduced-motion: reduce)").matches'), true);
        const beforeStall = connections;
        for (const gap of [150, 300, 750]) {
            const beforeRecovery = resyncs;
            silent = true; await pause(gap); silent = false; await pause(500);
            assert.equal(connections, beforeStall, `${gap} ms jitter must not reconnect`);
            assert.equal(resyncs, beforeRecovery, 'Fresh capture after a delivery gap needs no sync barrier');
            assert.equal(await evaluate('document.getElementById("status").textContent'), '正在播放');
            assert(Number(await evaluate('document.getElementById("buffer").textContent')) <= 151);
        }
        const beforeBusy = resyncs;
        await evaluate('(() => { const end = performance.now() + 400; while (performance.now() < end) {} })()');
        await pause(500);
        assert.equal(connections, beforeStall, 'Busy browser main thread must not reconnect');
        assert(resyncs > beforeBusy);
        assert.equal(invalidAcks, 0, 'Pre-marker audio must be discarded, not acknowledged');
        await evaluate('document.getElementById("advanced").click()', true);
        silent = true; await pause(4500); silent = false; await pause(2000);
        assert(connections > beforeStall, 'Silent transport should reconnect');
        assert.equal(await evaluate('document.getElementById("status").textContent'), '正在播放');
        busy = true;
        for (const socket of peers) socket.destroy();
        await pause(1300);
        await checkStates('reconnecting');
        busy = false;
        await pause(1500);
        assert.equal(await evaluate('document.getElementById("status").textContent'), '正在播放');
        // Observe production scheduling without adding browser globals to the app.
        await evaluate(`(async () => {
            const { PcmPlayer } = await import('/pcm-player.js');
            const pump = PcmPlayer.prototype.pump;
            PcmPlayer.prototype.pump = function (...args) {
                const result = pump.apply(this, args);
                window.playbackSnapshot = {
                    time: this.context.currentTime, state: this.context.state,
                    received: this.stats.received, buffered: this.bufferedMs,
                    scheduled: this.scheduledMs, underruns: this.underruns,
                };
                return result;
            };
        })()`);
        await command('Emulation.setDeviceMetricsOverride', {
            width: 1440, height: 900, deviceScaleFactor: 2, mobile: false,
        });
        await command('Page.bringToFront');
        await pause(200);
        const backgroundConnections = connections;
        let previousAcks = acknowledgements;
        let previousPlayback = await evaluate('playbackSnapshot');
        const otherTab = await command('Target.createTarget', { url: 'about:blank' });
        try {
            await command('Target.activateTarget', { targetId: otherTab.targetId });
            for (let i = 0; i < 30 && !await evaluate('document.hidden'); i++) await pause(100);
            assert.equal(await evaluate('document.hidden'), true, 'A real tab switch must hide the player');
            assert.equal(await evaluate('activeIntervals.size'), 1, 'Only audio pumping continues in the background');
            const backgroundFrame = await evaluate('document.getElementById("spectrum").toDataURL()');
            for (let i = 0; i < 12; i++) {
                await pause(5000);
                const playback = await evaluate('playbackSnapshot');
                assert.equal(await evaluate('document.hidden'), true);
                assert(acknowledgements > previousAcks + 100, 'Background audio must keep acknowledging frames');
                assert.equal(playback.state, 'running');
                assert(playback.time > previousPlayback.time + 4, 'The audio clock must keep advancing');
                assert(playback.received > previousPlayback.received + 100);
                assert(playback.scheduled > 0 && playback.buffered <= 151, JSON.stringify(playback));
                assert.equal(connections, backgroundConnections, 'Hiding must preserve the connection');
                previousAcks = acknowledgements; previousPlayback = playback;
            }
            assert.equal(await evaluate('document.getElementById("spectrum").toDataURL()'), backgroundFrame,
                'Background presentation must stay paused');
            for (const socket of peers) socket.destroy();
            for (let i = 0; i < 50 && acknowledgements <= previousAcks + 100; i++) await pause(100);
            assert(connections > backgroundConnections, 'Background transport failure must reconnect');
            assert(acknowledgements > previousAcks + 100, 'Background reconnect must resume audio');
            assert.equal(await evaluate('document.hidden'), true);
            console.log('Real hidden-tab playback passed: 60 seconds plus background reconnect');
        } finally {
            await command('Target.closeTarget', { targetId: otherTab.targetId });
            await command('Page.bringToFront');
        }
        const returnedConnections = connections;
        await pause(300);
        assert.equal(await evaluate('document.hidden'), false);
        assert.equal(connections, returnedConnections, 'Returning must reuse the background connection');
        assert.equal(await evaluate('activeIntervals.size'), 2);
        const beforePageReturn = connections;
        await evaluate("window.dispatchEvent(new PageTransitionEvent('pagehide', { persisted: true }))");
        await pause(200);
        assert.equal(await evaluate('activeIntervals.size'), 0, 'Leaving the page must release playback and statistics timers');
        const hiddenAcks = acknowledgements;
        await pause(200);
        assert.equal(acknowledgements, hiddenAcks, 'Leaving the page must release the stream');
        await evaluate("window.dispatchEvent(new PageTransitionEvent('pageshow', { persisted: true }))", true);
        await pause(800);
        await evaluate("window.dispatchEvent(new PageTransitionEvent('pageshow', { persisted: true }))", true);
        await pause(100);
        assert.equal(connections, beforePageReturn + 1, 'Returning must create exactly one connection');
        assert.equal(await evaluate('activeIntervals.size'), 2, 'Returning must retain only one playback and one statistics timer');
        assert.equal(await evaluate('document.getElementById("status").textContent'), '正在播放');
        await evaluate('document.getElementById("stop").click()');
        await pause(100);
        const stoppedConnections = connections;
        await evaluate(`window.dispatchEvent(new PageTransitionEvent('pagehide', { persisted: true }));
            window.dispatchEvent(new PageTransitionEvent('pageshow', { persisted: true }));`);
        const stoppedAcks = acknowledgements;
        await pause(1100);
        assert.equal(acknowledgements, stoppedAcks);
        assert.equal(connections, stoppedConnections, 'Returning must preserve an explicit Stop');
        assert.equal(await evaluate('activeIntervals.size'), 1, 'Stopped page must retain only the statistics timer');
        assert.equal(await evaluate('document.getElementById("status").textContent'), '已停止');
        await pause(3200); // Allow reduced-motion smoothing to settle to the idle baseline.
        const idle = await evaluate(`(() => {
            const canvas = document.getElementById('spectrum');
            const bytes = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height * 0.35).data;
            return bytes.some((value, index) => index % 4 === 3 && value > 0);
        })()`);
        assert.equal(idle, false, 'Stopped audio must not leave an animated spectrum');
        await checkStates('stopped');
        await checkStates('keyboard-focus', true);
        const contrast = await evaluate(`(() => {
            const luminance = color => {
                const values = color.match(/[\\d.]+/g).slice(0, 3).map(Number).map(x => {
                    x /= 255; return x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4;
                });
                return values[0] * 0.2126 + values[1] * 0.7152 + values[2] * 0.0722;
            };
            const ratio = (a, b) => { const x = luminance(a), y = luminance(b); return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05); };
            const background = element => {
                while (element) {
                    const color = getComputedStyle(element).backgroundColor;
                    if (color !== 'rgba(0, 0, 0, 0)') return color;
                    element = element.parentElement;
                }
            };
            const texts = [...document.querySelectorAll('h1, #status, dt, dd, dd small, button')]
                .filter(el => el.getClientRects().length).map(el => ratio(getComputedStyle(el).color, background(el)));
            const controls = [...document.querySelectorAll('button')].filter(el => el.getClientRects().length)
                .map(el => ratio(getComputedStyle(el).borderTopColor, background(el.parentElement)));
            const focused = document.querySelector(':focus-visible');
            return { text: Math.min(...texts), control: Math.min(...controls),
                focus: ratio(getComputedStyle(focused).outlineColor, background(focused.parentElement)) };
        })()`);
        assert(contrast.text >= 4.5, JSON.stringify(contrast));
        assert(contrast.control >= 3 && contrast.focus >= 3, JSON.stringify(contrast));
        console.log('Minimum contrast ratios:', contrast);
        const restingFrame = await evaluate('document.getElementById("spectrum").toDataURL()');
        await pause(300);
        assert.equal(await evaluate('document.getElementById("spectrum").toDataURL()'), restingFrame, 'Idle ribbons must be static');
        await evaluate('document.getElementById("language").click()', true);
        assert.equal(await evaluate('document.getElementById("status").textContent'), 'Stopped');
        assert.equal(await evaluate('localStorage.getItem("cardputer-language")'), 'en');
        const beforeReload = connections;
        await command('Page.reload');
        await pause(500);
        assert.equal(await evaluate('document.documentElement.lang'), 'en', 'Saved choice must override Chinese browser locale after reload');
        assert.equal(await evaluate('document.getElementById("advanced").textContent'), 'Advanced');
        await evaluate('document.getElementById("stop").click()');
        assert(connections <= beforeReload + 1);
        assert.deepEqual(errors, []);
        console.log(`Chrome page/Web Audio smoke test passed (autoplay=${allowAutoplay ? 'allowed' : 'blocked'}, ${acknowledgements} ACKs, ${resyncs} resyncs, ${connections} connections)`);
    } finally {
        if (cdp) cdp.close();
        for (const socket of peers) socket.destroy();
        server.closeAllConnections(); server.close();
        if (chrome && chrome.exitCode === null && chrome.pid) { chrome.kill(); await once(chrome, 'exit'); }
        fs.rmSync(profile, { recursive: true, force: true });
    }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
