export const SAMPLE_RATE = 16000;
export const FRAME_SAMPLES = 160;
export const FRAME_SECONDS = FRAME_SAMPLES / SAMPLE_RATE;

export function decodeFrame(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength !== 328) {
        throw new Error('Invalid audio frame (refresh the page after updating firmware)');
    }
    const view = new DataView(data);
    const pcm = new Float32Array(FRAME_SAMPLES);
    for (let i = 0; i < pcm.length; i++) pcm[i] = view.getInt16(8 + i * 2, true) / 32768;
    return { sequence: view.getUint32(0, true), capturedMs: view.getUint32(4, true), pcm };
}
