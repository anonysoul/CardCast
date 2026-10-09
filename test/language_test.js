'use strict';
const assert = require('node:assert/strict');
const { test, before } = require('node:test');
let chooseLanguage, LanguagePreference, localizeMessage, UI_TEXT;
before(async () => {
    ({ chooseLanguage, LanguagePreference, localizeMessage, UI_TEXT } = await import('../web/i18n.js'));
});

test('saved choice wins; Chinese locales use simplified Chinese and all others English', () => {
    for (const locale of ['zh', 'zh-CN', 'zh-TW', 'ZH-HK']) assert.equal(chooseLanguage(null, locale), 'zh');
    for (const locale of ['en-US', 'fr-FR', '', undefined]) assert.equal(chooseLanguage(null, locale), 'en');
    assert.equal(chooseLanguage('en', 'zh-CN'), 'en');
    assert.equal(chooseLanguage('zh', 'en-US'), 'zh');
    assert.equal(chooseLanguage('invalid', 'zh-TW'), 'zh');
});
test('language selection persists and works when storage getter or methods throw', () => {
    const values = new Map();
    const storage = { getItem: key => values.get(key), setItem: (key, value) => values.set(key, value) };
    const first = new LanguagePreference('en', () => storage); first.toggle();
    assert.equal(new LanguagePreference('en', () => storage).language, 'zh');
    for (const getStorage of [() => { throw Error('blocked'); }, () => ({ getItem() { throw Error(); }, setItem() { throw Error(); } })]) {
        const preference = new LanguagePreference('zh', getStorage);
        preference.toggle(); assert.equal(preference.language, 'en');
        preference.toggle(); assert.equal(preference.language, 'zh');
    }
});
test('status and event translation preserves raw values and error details', () => {
    const cases = [
        ['Playing', '正在播放'], ['Stopped', '已停止'], ['None', '无'],
        ['Audio suspended. Tap to resume.', '点击音波开启声音'],
        ['Disconnected (ack_timeout); retrying…', '连接已断开（ack_timeout），正在重试…'],
        ['Cannot start audio: raw error', '无法启动音频： raw error'],
        ['Discarded stale stream (300 ms excess delay) without reconnecting', '已丢弃过期音频（额外延迟 300 ms），连接保持'],
        ['400 ms excess stream delay; resynchronizing', '音频额外延迟 400 ms，正在重新同步'],
    ];
    for (const [raw, zh] of cases) {
        assert.equal(localizeMessage(raw, 'zh'), zh); assert.equal(localizeMessage(raw, 'en'), raw);
    }
    assert.deepEqual(Object.keys(UI_TEXT.zh), Object.keys(UI_TEXT.en));
});
