// SPDX-License-Identifier: MIT
"use strict";
const assert = require('node:assert/strict');

module.exports = async function resumeCases({evaluate, until, urls, rows, a, b}) {
    const count = () => evaluate(`document.querySelectorAll('#all .logs-line').length`);
    const source = async value => {
        await evaluate(`document.getElementById('logs-source').value=${JSON.stringify(value)};document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
        await until(() => evaluate(`window.logTestStreams.at(-1)?.testReplayed===true`));
    };
    const reconnect = async action => {
        const before = urls.length;
        await evaluate(action);
        await until(() => urls.length > before);
        await until(() => evaluate(`window.logTestStreams.at(-1)?.testReplayed===true`));
    };
    await source('all'); assert.equal(await count(), 5);
    for (let i = 0; i < 3; i++) {
        await source('local'); assert.equal(await count(), 1);
        await source('all'); assert.equal(await count(), 5);
        await source('usb:' + a); assert.equal(await count(), 1);
        await source('usb:' + b); assert.equal(await count(), 1);
        await source('usb:' + a); assert.equal(await count(), 1);
        assert(!new URL(urls.at(-1), 'http://fixture').searchParams.has('cursor'));
    }
    await source('all');
    await evaluate(`localStorage.setItem(window.WSPRRYPI_LOG_TEST_HOOKS.cursorStorageKey(),'another-tab-tail')`);
    await reconnect(`document.getElementById('btn-reconnect').click()`);
    assert.equal(await count(), 5, 'Reconnect retains rows; a valid tail emits no old records');
    assert.equal(new URL(urls.at(-1), 'http://fixture').searchParams.get('cursor'), 'f-1');
    await reconnect(`window.dispatchEvent(new Event('offline'));window.dispatchEvent(new Event('online'))`);
    assert.equal(await count(), 5, 'Offline feedback must not erase retained rows');
    await evaluate(`document.getElementById('btn-clear').click()`);
    await reconnect(`document.getElementById('btn-reconnect').click()`);
    assert.equal(await count(), 0, 'Clear remains cleared through a reconnect');
    assert(await evaluate(`document.getElementById('all').textContent.includes('Log output cleared')`));
    await source('usb:' + a); assert.equal(await count(), 1);
    await evaluate(`document.getElementById('logs-pause').click()`);
    const paused = urls.length;
    await evaluate(`document.getElementById('logs-source').value='usb:${b}';document.getElementById('logs-source').dispatchEvent(new Event('change'))`);
    assert.equal(urls.length, paused); assert.equal(await count(), 0);
    await reconnect(`document.getElementById('logs-pause').click()`);
    assert.equal(await count(), 1, 'A changed paused context loads history on resume');
    assert(!new URL(urls.at(-1), 'http://fixture').searchParams.has('cursor'));
    await source('all');
    await evaluate(`window.staleLogStream=window.logTestStreams.at(-1)`);
    await source('local');
    const current = await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.viewState()`);
    await evaluate(`window.staleLogStream.dispatchEvent(new MessageEvent('journal',{data:JSON.stringify({type:'journal',__CURSOR:'stale',MESSAGE:'STALE RECORD'})}));window.staleLogStream.dispatchEvent(new Event('playback_start'));window.staleLogStream.dispatchEvent(new Event('error'))`);
    assert.deepEqual(await evaluate(`window.WSPRRYPI_LOG_TEST_HOOKS.viewState()`), current);
    assert.equal(await count(), 1);
    await source('all');
    await evaluate(`location.reload()`);
    await until(() => evaluate(`window.logTestStreams?.at(-1)?.testReplayed===true && document.querySelectorAll('#all .logs-line').length===5`));
    assert(!new URL(urls.at(-1), 'http://fixture').searchParams.has('cursor'), 'Reload ignores persisted cursors for absent rows');

    rows.push(...[
        ['UTC synchronized', '6'], ['UTC holdover', '7'], ['UTC unavailable', '4']
    ].map(([MESSAGE, PRIORITY], index) => ({source:'usb:' + a, origin_kind:'pico', origin_id:a,
        origin_name:'pico-a.local', origin_boot:'4'.repeat(32), origin_sequence:String(20 + index),
        MESSAGE, PRIORITY, __CURSOR:'clock-' + index})));
    await source('usb:' + a); assert.equal(await count(), 4);
    for (const [severity, expected] of [['6',3], ['4',1], ['3',0], ['7',4]]) {
        await evaluate(`document.getElementById('logs-severity').value='${severity}';document.getElementById('logs-severity').dispatchEvent(new Event('change'))`);
        await until(() => evaluate(`window.logTestStreams.at(-1)?.testReplayed===true`));
        assert.equal(await count(), expected);
        if (severity === '4') assert(await evaluate(`document.querySelector('#all .logs-line').textContent.includes('UTC unavailable')`));
        assert(!new URL(urls.at(-1), 'http://fixture').searchParams.has('cursor'));
    }
    await source('local');
    await evaluate(`document.getElementById('logs-severity').value='3';document.getElementById('logs-severity').dispatchEvent(new Event('change'))`);
    await until(() => evaluate(`window.logTestStreams.at(-1)?.testReplayed===true`));
    await source('all');
    assert.equal(await count(), 0);
    assert(await evaluate(`!document.getElementById('all').textContent.includes('choose All sources')`));
};
