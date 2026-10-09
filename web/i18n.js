const LANGUAGE_KEY = 'cardputer-language';
export const UI_TEXT = {
    "zh": {
        "title": "CardCast · 实时音频",
        "spectrum": "极光声波",
        "spectrumLabel": "随播放声音舒展的橙色、琥珀色和暖白色丝带",
        "unlock": "播放",
        "unlockHint": "开始监听",
        "advanced": "高级",
        "collapse": "收起",
        "details": "高级音频信息",
        "start": "开始",
        "stop": "停止",
        "buffer": "播放缓冲",
        "target": "目标缓冲",
        "dropped": "丢弃 / 缺失帧",
        "rate": "近 10 秒丢弃 / 缺失率",
        "missing": "到达前缺失",
        "trimmed": "播放队列裁剪",
        "stale": "过期音频丢弃",
        "underruns": "缓冲耗尽",
        "recoveries": "恢复次数",
        "reconnects": "重连次数",
        "switchLanguage": "当前语言：中文，点击切换为英文"
    },
    "en": {
        "title": "CardCast · Live Audio",
        "spectrum": "Aurora audio ribbons",
        "spectrumLabel": "Orange, amber and warm white ribbons responding to live audio",
        "unlock": "Play",
        "unlockHint": "Start listening",
        "advanced": "Advanced",
        "collapse": "Collapse",
        "details": "Advanced audio information",
        "start": "Start",
        "stop": "Stop",
        "buffer": "Playback buffer",
        "target": "Target buffer",
        "dropped": "Discarded / missing frames",
        "rate": "Discard / missing rate (last 10 s)",
        "missing": "Missing before arrival",
        "trimmed": "Playback queue trims",
        "stale": "Stale-stream discards",
        "underruns": "Buffer underruns",
        "recoveries": "Recoveries",
        "reconnects": "Reconnect attempts",
        "switchLanguage": "Current language: English. Click to switch to Chinese"
    }
};

export function chooseLanguage(saved, preferred) {
    if (saved === 'zh' || saved === 'en') return saved;
    return /^zh(?:-|$)/i.test(preferred || '') ? 'zh' : 'en';
}

export class LanguagePreference {
    constructor(preferred, getStorage = () => window.localStorage) {
        this.getStorage = getStorage;
        let saved;
        try { saved = getStorage().getItem(LANGUAGE_KEY); } catch (_) { /* Storage is optional. */ }
        this.language = chooseLanguage(saved, preferred);
    }
    toggle() {
        this.language = this.language === 'zh' ? 'en' : 'zh';
        try { this.getStorage().setItem(LANGUAGE_KEY, this.language); } catch (_) { /* Keep the in-memory choice. */ }
    }
    text(key) { return UI_TEXT[this.language][key]; }
}

const STATUS_ZH = {
    'Starting…': '正在启动…', 'None': '无',
    'WebSocket transport error': 'WebSocket 传输错误',
    'Stopped': '已停止', 'Playing': '正在播放', 'Buffering…': '正在缓冲…',
    'Connecting…': '正在连接…',
    'Audio suspended. Tap to resume.': '点击音波开启声音',
    'Connection failed; retrying…': '连接失败，正在重试…',
    'Device busy; retrying…': '设备正在使用中，正在重试…',
    'Stream unavailable; reconnecting…': '音频流不可用，正在重连…',
    'Audio resync timed out; reconnecting…': '音频同步超时，正在重连…',
    'Invalid stream; reconnecting…': '音频格式无效，正在重连…',
    'Resuming live audio…': '正在恢复实时音频…',
    'Catching up to live audio…': '正在同步实时音频…',
};

export function localizeMessage(message, language) {
    if (language === 'en') return message;
    return STATUS_ZH[message]
        || message.replace('Cannot start audio:', '无法启动音频：')
            .replace('Disconnected (', '连接已断开（').replace('); retrying…', '），正在重试…')
            .replace(/Discarded stale stream \((\d+) ms excess delay\) without reconnecting/, '已丢弃过期音频（额外延迟 $1 ms），连接保持')
            .replace(/(\d+) ms excess stream delay; resynchronizing/, '音频额外延迟 $1 ms，正在重新同步');
}
