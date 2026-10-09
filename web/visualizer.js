// Visual energy only: these values never change playback gain or buffering.
export class AudioEnvelope {
    constructor() { this.values = new Float32Array(4); }
    reset() { this.values.fill(0); }
    update(samples, bins, sampleRate, elapsed, active) {
        let square = 0;
        if (active) for (const sample of samples) square += sample * sample;
        const rms = Math.sqrt(square / samples.length);
        const energy = active ? Math.min(1, Math.log1p(Math.max(0, rms - 0.0005) * 120) / Math.log(10)) : 0;
        for (let band = 0; band < 4; band++) {
            let target = energy;
            if (band > 0) {
                const edges = [0, 80, 400, 2000, 8000];
                const from = Math.max(1, Math.floor(edges[band] * samples.length / sampleRate));
                const to = Math.min(bins.length, Math.ceil(edges[band + 1] * samples.length / sampleRate));
                let peak = 0;
                if (active) for (let i = from; i < to; i++) peak = Math.max(peak, bins[i]);
                target *= peak / 255;
            }
            // Time-based smoothing stays consistent across frame rates.
            const seconds = target > this.values[band] ? 0.08 : 0.38;
            this.values[band] += (target - this.values[band]) * (1 - Math.exp(-elapsed / seconds));
            if (this.values[band] < 0.0001) this.values[band] = 0;
        }
        return this.values;
    }
}

// Four translucent ribbons read the output analyser, never the playback queue.
export class SpectrumView {
    constructor(canvas, stream) {
        this.canvas = canvas;
        this.stream = stream;
        this.paint = canvas.getContext('2d');
        this.bins = new Uint8Array(512);
        this.samples = new Float32Array(1024);
        this.envelope = new AudioEnvelope();
        this.motion = matchMedia('(prefers-reduced-motion: reduce)');
        this.phase = 0;
        this.frame = null;
        this.lastDraw = null;
        this.top = new Float32Array(161);
        this.bottom = new Float32Array(161);
        this.resize = new ResizeObserver(() => this.measure());
        this.resize.observe(canvas);
        this.measure();
    }
    measure() {
        const { width, height } = this.canvas.getBoundingClientRect();
        this.width = width; this.height = height;
        const ratio = Math.min(devicePixelRatio || 1, 2);
        this.canvas.width = Math.round(width * ratio);
        this.canvas.height = Math.round(height * ratio);
        this.paint.setTransform(ratio, 0, 0, ratio, 0, 0);
        // Resolve the CSS palette only when geometry changes, never per frame.
        const style = getComputedStyle(this.canvas);
        const orange = style.getPropertyValue('--ribbon-orange').trim();
        const amber = style.getPropertyValue('--ribbon-amber').trim();
        const white = style.getPropertyValue('--ribbon-white').trim();
        this.gradients = [0, 1, 2, 3].map(layer => {
            const gradient = this.paint.createLinearGradient(0, 0, width, 0);
            gradient.addColorStop(0, `${orange}00`);
            gradient.addColorStop(0.18, layer % 2 ? amber : orange);
            gradient.addColorStop(0.5, layer % 2 ? white : amber);
            gradient.addColorStop(0.82, white);
            gradient.addColorStop(1, `${white}00`);
            return gradient;
        });
    }
    setVisible(visible) {
        if (this.frame !== null) cancelAnimationFrame(this.frame);
        this.frame = null;
        this.lastDraw = null;
        if (!visible) { this.envelope.reset(); return; }
        const tick = now => {
            if (this.lastDraw === null || now - this.lastDraw >= (this.motion.matches ? 125 : 33)) {
                const elapsed = this.lastDraw === null ? 1 / 30 : Math.min(0.25, (now - this.lastDraw) / 1000);
                this.draw(elapsed); this.lastDraw = now;
            }
            this.frame = requestAnimationFrame(tick);
        };
        this.frame = requestAnimationFrame(tick);
    }
    draw(elapsed) {
        const { paint, width, height, stream } = this;
        const active = stream.visible && stream.context?.state === 'running' && stream.player?.scheduledMs > 0;
        this.bins.fill(0); this.samples.fill(0);
        if (active) {
            stream.player.analyser.getByteFrequencyData(this.bins);
            stream.player.analyser.getFloatTimeDomainData(this.samples);
        }
        const [energy, low, mid, high] = this.envelope.update(this.samples, this.bins, stream.context?.sampleRate || 48000, elapsed, active);
        if (!this.motion.matches && energy > 0) this.phase += elapsed * (0.45 + energy * 0.35);
        const phase = this.motion.matches ? 0 : this.phase;
        paint.clearRect(0, 0, width, height);
        // One quiet hairline remains when there is no sound, with no idle motion.
        if (energy === 0) {
            paint.beginPath(); paint.moveTo(0, height / 2); paint.lineTo(width, height / 2);
            paint.strokeStyle = this.gradients[0]; paint.lineWidth = 0.7; paint.globalAlpha = 0.24;
            paint.stroke(); paint.globalAlpha = 1; return;
        }
        const amplitude = Math.min(height * 0.34, width * 0.32);
        for (let layer = 3; layer >= 0; layer--) {
            const offset = layer * 0.95;
            for (let i = 0; i <= 160; i++) {
                const t = i / 160, taper = Math.pow(Math.sin(Math.PI * t), 1.8);
                const wave = Math.sin(t * Math.PI * (2.2 + low * 0.6) - phase + offset) * 0.72
                    + Math.sin(t * Math.PI * 4.4 + phase * 0.65 + offset) * (0.15 + mid * 0.12)
                    + Math.sin(t * Math.PI * 7 - phase * 0.4) * high * 0.08;
                const center = height / 2 + taper * amplitude * energy * wave * (1 - layer * 0.12);
                const breadth = taper * amplitude * energy * (0.10 + mid * 0.12) * (0.6 + 0.4 * Math.sin(t * Math.PI + offset) ** 2);
                this.top[i] = center - breadth;
                this.bottom[i] = center + breadth;
            }
            const edge = new Path2D(), ribbon = new Path2D();
            edge.moveTo(0, this.top[0]); ribbon.moveTo(0, this.top[0]);
            for (let i = 1; i <= 160; i++) {
                edge.lineTo(i * width / 160, this.top[i]);
                ribbon.lineTo(i * width / 160, this.top[i]);
            }
            for (let i = 160; i >= 0; i--) ribbon.lineTo(i * width / 160, this.bottom[i]);
            ribbon.closePath();
            paint.fillStyle = paint.strokeStyle = this.gradients[layer];
            paint.globalAlpha = (layer === 0 ? 0.14 : 0.065) * (0.45 + energy);
            paint.fill(ribbon);
            // Broad, faint strokes provide a restrained halo without blur filters.
            if (!this.motion.matches) {
                paint.globalAlpha = layer === 0 ? 0.045 : 0.02;
                paint.lineWidth = 7; paint.stroke(edge);
            }
            paint.globalAlpha = layer === 0 ? 0.85 : 0.30 + (3 - layer) * 0.1;
            paint.lineWidth = layer === 0 ? 1.35 : 0.8;
            paint.stroke(edge);
        }
        paint.globalAlpha = 1;
    }
}
