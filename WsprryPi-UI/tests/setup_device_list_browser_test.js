// SPDX-License-Identifier: MIT
// Synthetic UI evidence only: no discovery, device connection, or hardware access.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const h = require('./browser_harness.js');

(async () => {
    const ui = path.resolve(__dirname, '..');
    let php, browser, client, profile, failure;
    try {
        const port = await h.freePort();
        php = h.spawnFixtureChild('php', ['-S', `127.0.0.1:${port}`, '-t', 'data', 'tests/browser_fixture_router.php'], {cwd: ui});
        await h.waitForSpawn(php);
        await h.waitForPhp(php, `http://127.0.0.1:${port}/index.php?page=config`);
        ({browser, client, profile} = await h.launchBrowser({args: ['--headless=new', '--no-sandbox', '--disable-gpu', '--no-first-run']}));
        await client.send('Page.navigate', {url: `http://127.0.0.1:${port}/index.php?page=config`});
        await h.waitFor(() => client.evaluate("document.readyState === 'complete' && !!window.SetupTargets && window.ControllerMode?.transmitter"), 'Setup initialized on a verified host');
        // Keep the real Setup renderer and templates; isolate synthetic inventory
        // from application recovery and editor requests, all refused by this router.
        await client.evaluate(`
            window.ConfigurationRecovery = {active:false};
            window.ControllerMode = {active:false};
            window.PicoFirmwareSafety = {locked:false};
            window.WtpUi = {catalogReady:true,explore(){}};
            window.PicoTargetEditors = {leave(){},retainedProfiles(){return []}};
            window.PiTargetEditors = {leave(){},retainedTargets(){return []}};
            window.SchedulingEditor = {leave(){},retainedTargets(){return []}};
            window.hasUnsavedLocalConfigChanges = () => false;
            window.suspendConfigAutosave = () => {};
            SetupTargets.localLoaded();
        `);
        const network = {id:'net-a',instance:'Bench Pico',binding:'consumer',state:'online',target:'bench.local.',port:80};
        const usb = {id:'usb-a',instance:'USB Pico',binding:'usb',state:'online',settings:{Transport:'usb','USB Serial':'PICO123','USB Vendor ID':11914,'USB Product ID':10}};
        const observations = [
            {id:'printer',instance:'HP OfficeJet',binding:'',state:'invalid'},
            {id:'printer-resolving',instance:'Zebra printer',state:'resolving'},
            {...network,state:'removed',id:'removed'},
            {...network,binding:'unexpected',id:'unknown'},
            {...network,port:8080,id:'wrong-port'},
            {...usb,settings:{Transport:'usb'},id:'incomplete-usb'},
            network, {...network,id:'net-b',target:'BENCH.LOCAL',interface:2,protocol:1},
            usb, {...usb,id:'usb-b'},
            {...network,id:'other',target:'other.local',instance:'Bench Pico'},
        ];
        const catalog = async (profiles, candidates) => client.evaluate(`SetupTargets.catalogChanged(${JSON.stringify(profiles)},${JSON.stringify(candidates)})`);
        const options = () => client.evaluate("Array.from(document.getElementById('setup-device').options, option => ({value:option.value,text:option.textContent}))");
        await catalog([], observations);
        let list = await options();
        assert.deepEqual(list.map(option => option.value), ['local','nearby:net-a','nearby:usb-a','nearby:other']);
        assert.match(list[1].text, /Wi-Fi/);
        assert.match(list[2].text, /USB/);
        assert(!list.some(option => /printer|OfficeJet|Zebra/.test(option.text)));
        // Distinct devices with the same display name must remain distinct.
        assert.equal(list.filter(option => option.text.startsWith('Bench Pico')).length, 2);
        assert.match(list[1].text, /bench.local/);
        assert.match(list[3].text, /other.local/);
        await client.evaluate("document.getElementById('setup-device').focus()");
        await catalog([], observations);
        assert.equal(await client.evaluate("document.activeElement.id"), 'setup-device');
        // Prefer a selected link observation over another observation of that endpoint.
        await client.evaluate("document.getElementById('setup-device').value='nearby:net-a';document.getElementById('setup-device').dispatchEvent(new Event('change'))");
        await catalog([], [observations[7], network]);
        list = await options();
        assert.deepEqual(list.map(option => option.value), ['local','nearby:net-a']);
        assert.equal(await client.evaluate("document.getElementById('setup-device').value"), 'nearby:net-a');
        await client.evaluate("document.getElementById('setup-device').value='local';document.getElementById('setup-device').dispatchEvent(new Event('change'));bootstrap.Tab.getOrCreateInstance(document.getElementById('radio-tab')).show()");
        const saved = [
            {id:'a'.repeat(32),name:'Saved Wi-Fi Pico',settings:{'Device ID':'b'.repeat(32),Transport:'network_plain',Hostname:'BENCH.LOCAL','TCP Port':80}},
            {id:'c'.repeat(32),name:'Saved USB Pico',settings:{...usb.settings,'Device ID':'d'.repeat(32)}},
            {id:'e'.repeat(32),name:'Saved TLS Pico',discovery_id:'other',settings:{'Device ID':'f'.repeat(32),Transport:'network'}},
        ];
        await catalog(saved, observations);
        list = await options();
        assert.deepEqual(list.map(option => option.value), ['local',...saved.map(item => 'known:'+item.id)]);
        assert.match(list[1].text, /Network \(unencrypted\)/);
        assert.match(list[2].text, /USB/);
        assert.match(list[3].text, /Network \(TLS\)/);
        await catalog(saved, [network,usb,{...network,id:'other',target:'other.local'}]);
        // Retained Pi contexts can arrive from both editors; present one entry.
        await client.evaluate(`window.PiTargetEditors.retainedTargets=()=>[{device_id:'${'1'.repeat(32)}',name:'Paired Pi'}];window.SchedulingEditor.retainedTargets=window.PiTargetEditors.retainedTargets;SetupTargets.render()`);
        assert.equal((await options()).filter(option => option.value.startsWith('pi:')).length, 1);
        await catalog([], observations);
        const output = process.env.WSPRRYPI_SETUP_LIST_SCREENSHOTS || fs.mkdtempSync(path.join(os.tmpdir(), 'setup-device-list-captures-'));
        fs.mkdirSync(output, {recursive:true});
        // Expand the native select in this presentation fixture so captures show
        // every label without relying on an OS-owned popup screenshot.
        await client.evaluate("document.getElementById('setup-device').size=5;document.getElementById('setup-device').style.height='auto'");
        for (const width of [1440,390]) for (const theme of ['light','dark']) {
            await client.send('Emulation.setDeviceMetricsOverride', {width,height:1000,deviceScaleFactor:1,mobile:width<500});
            await client.evaluate(`document.documentElement.setAttribute('data-bs-theme','${theme}');document.getElementById('setup-device').focus({preventScroll:true});window.scrollTo(0,0)`);
            await client.settleRendering();
            const shot = await client.send('Page.captureScreenshot', {format:'png'});
            fs.writeFileSync(path.join(output, `${theme}-${width}.png`), Buffer.from(shot.data,'base64'));
        }
        console.log('PASS Setup list: rejects unrelated/malformed/offline observations; Wi-Fi/USB/network labels; endpoint deduplication; saved suppression; same-name devices; selection/focus; retained Pi deduplication; four renders: '+output);
    } catch (error) { failure = error; }
    finally { await h.finishFixture({failure,php,browser,client,profile}); }
})().catch(error => { console.error(error); process.exitCode = 1; });
