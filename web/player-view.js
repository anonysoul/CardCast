import { localizeMessage } from './i18n.js';

// Own DOM rendering here; audio and transport modules never depend on the page.
export class PlayerView {
    constructor(document, language) {
        this.document = document;
        this.language = language;
        this.currentStatus = 'Starting…';
        this.elements = Object.fromEntries([
            'status', 'unlock', 'language', 'advanced', 'details', 'start', 'stop',
            'spectrum', 'buffer', 'target-buffer', 'dropped', 'drop-rate', 'missing',
            'trimmed', 'resync-drops', 'underruns', 'recoveries', 'reconnects',
        ].map(id => [id, document.getElementById(id)]));
        this.textNodes = document.querySelectorAll('[data-i18n]');
        this.ariaNodes = document.querySelectorAll('[data-i18n-aria]');
    }

    setStatus(message) {
        this.currentStatus = message;
        const text = localizeMessage(message, this.language.language);
        if (this.elements.status.textContent !== text) this.elements.status.textContent = text;
    }

    setActivation(needed) {
        this.elements.unlock.hidden = !needed;
    }

    renderLanguage() {
        const { language, elements } = this;
        const locale = language.language === 'zh' ? 'zh-CN' : 'en';
        this.document.documentElement.lang = locale;
        this.textNodes.forEach(node => { node.textContent = language.text(node.dataset.i18n); });
        this.ariaNodes.forEach(node => { node.setAttribute('aria-label', language.text(node.dataset.i18nAria)); });
        elements.language.textContent = language.language === 'zh' ? '中文' : 'English';
        elements.language.lang = locale;
        elements.language.setAttribute('aria-label', language.text('switchLanguage'));
        elements.advanced.textContent = language.text(elements.details.hidden ? 'advanced' : 'collapse');
        this.setStatus(this.currentStatus);
    }

    toggleAdvanced() {
        const { advanced, details } = this.elements;
        const expanded = advanced.getAttribute('aria-expanded') !== 'true';
        advanced.setAttribute('aria-expanded', String(expanded));
        advanced.textContent = this.language.text(expanded ? 'collapse' : 'advanced');
        details.hidden = !expanded;
        this.document.body.classList.toggle('expanded', expanded);
    }

    renderStats(stream, now) {
        const { player } = stream;
        const values = {
            buffer: player ? player.bufferedMs.toFixed(0) : '0',
            dropped: player ? player.dropped : 0,
            missing: player ? player.stats.missing : 0,
            trimmed: player ? player.stats.trimmed : 0,
            'resync-drops': player ? player.stats.resync : 0,
            'drop-rate': player ? player.stats.recentDropPercent(now).toFixed(1) : '0.0',
            'target-buffer': player ? player.targetMs : 60,
            underruns: player ? player.underruns : 0,
            recoveries: stream.recoveries,
            reconnects: stream.reconnects,
        };
        for (const [id, value] of Object.entries(values)) this.elements[id].textContent = value;
    }
}
