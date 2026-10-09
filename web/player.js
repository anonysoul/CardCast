import { StreamController } from './stream-controller.js';
import { SpectrumView } from './visualizer.js';
import { LanguagePreference } from './i18n.js';
import { PlayerView } from './player-view.js';

const language = new LanguagePreference(navigator.languages?.[0] || navigator.language);
const view = new PlayerView(document, language);
const stream = new StreamController({
    makeContext: () => new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' }),
    makeSocket: url => new WebSocket(url),
    url: `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/audio`,
    status: message => view.setStatus(message),
    activation: needed => view.setActivation(needed),
});
const spectrum = new SpectrumView(view.elements.spectrum, stream);
let statsTimer = null;
let resumeOnReturn = false;

function updateStats() {
    view.renderStats(stream, performance.now());
}

function setPresentationVisible(visible) {
    spectrum.setVisible(visible);
    if (statsTimer !== null) clearInterval(statsTimer);
    statsTimer = null;
    updateStats();
    if (visible) statsTimer = setInterval(updateStats, 250);
}

view.elements.start.addEventListener('click', () => stream.start());
view.elements.stop.addEventListener('click', () => stream.stop());
view.elements.unlock.addEventListener('click', () => stream.start());
view.elements.advanced.addEventListener('click', () => view.toggleAdvanced());
view.elements.language.addEventListener('click', () => {
    language.toggle();
    view.renderLanguage();
});

document.addEventListener('visibilitychange', () => {
    stream.setVisible(!document.hidden);
    setPresentationVisible(!document.hidden);
});
window.addEventListener('pagehide', () => {
    resumeOnReturn = stream.wanted;
    stream.stop();
    setPresentationVisible(false);
});
window.addEventListener('pageshow', event => {
    if (!event.persisted) return;
    stream.visible = !document.hidden;
    setPresentationVisible(!document.hidden);
    if (resumeOnReturn) stream.start();
});

view.renderLanguage();
stream.visible = !document.hidden;
setPresentationVisible(!document.hidden);
stream.start();
