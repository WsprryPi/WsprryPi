// SPDX-License-Identifier: MIT
// Hardware-free rendered WTP network workflow with mocked HTTP responses.
"use strict";

const assert = require("node:assert/strict");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");
const { spawnFixtureChild, waitForSpawn, childDiagnostic, spawnIsolatedProcess, stopIsolatedProcess, stopChild } = require("./browser_fixture_process.js");
const WebSocket = require("ws");

const UI_ROOT = path.resolve(__dirname, "..");

function freePort() {
    return new Promise((resolve, reject) => {
        const server = net.createServer();
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            const { port } = server.address();
            server.close((error) => error ? reject(error) : resolve(port));
        });
    });
}

function getJson(url) {
    return new Promise((resolve, reject) => {
        http.get(url, (response) => {
            let body = "";
            response.setEncoding("utf8");
            response.on("data", (chunk) => { body += chunk; });
            response.on("end", () => {
                try {
                    resolve(JSON.parse(body));
                } catch (error) {
                    reject(error);
                }
            });
        }).on("error", reject);
    });
}

function getStatus(url) {
    return new Promise((resolve, reject) => {
        http.get(url, (response) => {
            response.resume();
            response.on("end", () => resolve(response.statusCode));
        }).on("error", reject);
    });
}

async function waitFor(check, description, timeoutMs = 10000) {
    const deadline = Date.now() + timeoutMs;
    let lastError;
    while (Date.now() < deadline) {
        try {
            const value = await check();
            if (value) return value;
        } catch (error) {
            lastError = error;
        }
        await new Promise((resolve) => setTimeout(resolve, 50));
    }
    throw new Error(`Timed out waiting for ${description}${lastError ? `: ${lastError.message}` : ""}`);
}

class CdpClient {
    constructor(url) {
        this.socket = new WebSocket(url);
        this.nextId = 1;
        this.pending = new Map();
        this.dialogs = [];
        this.dialogError = null;
        const reviewDialogs = new Set([
            "Review the current saved values beside your preserved draft. Matching values do not prove an earlier save committed. Send one new explicit save with this fresh revision?",
            "Review current saved and active observations beside your preserved draft. Send one new explicit save using the fresh revision?"
        ]);
        const failDialog = error => {
            this.dialogError = error;
            for (const { reject } of this.pending.values()) reject(error);
            this.pending.clear();
        };
        this.socket.on("message", (raw) => {
            const message = JSON.parse(raw);
            if (message.method === 'Page.javascriptDialogOpening') {
                const expected = message.params.type === 'confirm' && reviewDialogs.has(message.params.message);
                this.dialogs.push(message.params.message);
                if (!expected) failDialog(new Error(`Unexpected browser dialog: ${message.params.type}: ${message.params.message}`));
                void this.send('Page.handleJavaScriptDialog', { accept: expected }).catch(failDialog);
                return;
            }
            if (!message.id || !this.pending.has(message.id)) return;
            const { resolve, reject } = this.pending.get(message.id);
            this.pending.delete(message.id);
            if (message.error) reject(new Error(message.error.message));
            else resolve(message.result);
        });
    }

    async open() {
        if (this.socket.readyState === WebSocket.OPEN) return;
        await new Promise((resolve, reject) => {
            this.socket.once("open", resolve);
            this.socket.once("error", reject);
        });
    }

    send(method, params = {}) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.socket.send(JSON.stringify({ id, method, params }));
        });
    }

    close() {
        this.socket.close();
    }
}

async function main() {
    const fs = require('node:fs');
    const phpPort = await freePort(), debugPort = await freePort();
    const url = `http://127.0.0.1:${phpPort}/index.php?page=config&setup_tab=%23fleet-pane`;
    const profileDir = `/tmp/wsprrypi-network-ui-${process.pid}`;
    const output = path.resolve(UI_ROOT, '../src/build/wtp-network/ui');
    fs.mkdirSync(output, { recursive: true });
    const captureScreenshots = process.env.WSPRRYPI_UI_CAPTURE !== '0';
    const resourceBuild=path.dirname(output);
    const resourceFixtures={
        loaded:JSON.parse(fs.readFileSync(path.join(resourceBuild,'resources-loaded-engineering_mtls.json'))),
        consumer:JSON.parse(fs.readFileSync(path.join(resourceBuild,'resources-pending-consumer_station.json'))),
        fault:JSON.parse(fs.readFileSync(path.join(resourceBuild,'resources-fault-engineering_mtls.json')))
    };
    let chrome, client, php, failure;
    const capture = async (name, parameters = {format:'png'}) => {
        if (!captureScreenshots) return;
        const shot = await client.send('Page.captureScreenshot', parameters);
        fs.writeFileSync(path.join(output, name), Buffer.from(shot.data, 'base64'));
    };
    try {
        php = spawnFixtureChild('php', ['-S', `127.0.0.1:${phpPort}`, '-t', 'data', 'tests/browser_fixture_router.php'], { cwd: UI_ROOT });
        await waitForSpawn(php);
        await waitFor(async () => await getStatus(url) === 200, 'PHP fixture');
        chrome = spawnIsolatedProcess(process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'chromium'),
            ['--headless', '--no-sandbox', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-background-networking', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profileDir}`, url]);
        await waitForSpawn(chrome);
        // A fresh CI Chrome profile can take longer than workflow assertions to start.
        // Keep startup finite without changing any UI assertion deadline.
        const page = await waitFor(async () => (await getJson(`http://127.0.0.1:${debugPort}/json`)).find(p => p.type === 'page'), 'browser startup', 60000);
        client = new CdpClient(page.webSocketDebuggerUrl); await client.open();
        await client.send('Page.enable');
        const evaluate = async expression => {
            if (client.dialogError) throw client.dialogError;
            const result = await client.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
            if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
            return result.result.value;
        };
        await waitFor(async () => await evaluate('document.readyState === "complete" && !!window.WtpUi && !!window.WtpManagement'), 'WTP scripts');
        assert.equal(await evaluate('document.getElementById("fleet-tab-item").hidden'), true);
        assert.equal(await evaluate('document.getElementById("fleet-tab").classList.contains("active")'), false);
        assert.equal(await evaluate('new URL(location.href).searchParams.has("setup_tab")'), false);
        assert.equal(await evaluate('document.getElementById("fleet-tab").disabled'), true);
        assert.equal(await evaluate('document.getElementById("wtp-controls").closest("[role=tabpanel]").id'), 'fleet-pane');
        // Saved navigation values are literal targets, never CSS selectors.
        for (const target of ['#fleet-pane"]', '"] , #fleet-tab, [data-bs-target="x', '#missing-pane']) {
            await evaluate(`(() => {
                const tabs=document.getElementById('configTabs');
                const url=new URL(location.href);url.searchParams.set('setup_tab',${JSON.stringify(target)});
                history.replaceState(null,'',url);restorePersistedTabState(tabs);
            })()`);
            assert.equal(await evaluate('new URL(location.href).searchParams.has("setup_tab")'),false,'invalid tab URL removed');
            assert.equal(await evaluate('document.getElementById("fleet-tab").classList.contains("active")'),false);
        }
        await evaluate(`(() => {
            const tabs=document.getElementById('configTabs');
            window.__originalRestoreScope=tabs.dataset.persistTabStateScope;
            tabs.dataset.persistTabStateScope='always';
            localStorage.setItem(getPersistedTabStorageKey(tabs),'#fleet-pane"]');
            restorePersistedTabState(tabs);
        })()`);
        assert.equal(await evaluate('localStorage.getItem(getPersistedTabStorageKey(document.getElementById("configTabs")))'),null,'malformed stored target removed');
        await evaluate(`(() => {
            const tabs=document.getElementById('configTabs');
            const url=new URL(location.href);url.searchParams.set('setup_tab','#pi-hardware-pane');history.replaceState(null,'',url);
            restorePersistedTabState(tabs);
        })()`);
        await waitFor(async()=>await evaluate('document.getElementById("pi-hardware-tab").classList.contains("active")'),'visible saved tab restores');
        await evaluate(`(() => {
            const tabs=document.getElementById('configTabs');
            tabs.dataset.persistTabStateScope=window.__originalRestoreScope;
            bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();
            localStorage.removeItem(getPersistedTabStorageKey(tabs));clearPersistedTabUrlState(tabs);
        })()`);
        await waitFor(async()=>await evaluate('document.getElementById("transmitter-hardware-tab").classList.contains("active")'),'Transmitter restored after hostile tab inputs');
        await evaluate(`(() => {
            configAutosaveSuspended = true;
            clearPendingPopulateConfigRetry(); clearWebSocketReconnectTimer();
            backendCurrentlyConnected = true; websocketCurrentlyConnected = true;
            syncConnectionAlert(); clearBackendStatus('runtime'); clearConfigLoadFailureState();
            const style = document.createElement('style'); style.textContent='* { scroll-behavior: auto !important; }'; document.head.append(style);
            window.__resourceFixtures = ${JSON.stringify(resourceFixtures)};
            window.__resourceState = 'loaded';
            window.__requests = [];
            window.__remoteFailure = false;
            window.__statusFailure = false;
            window.__discoveryState = "active";
            window.__snapshot = { selected:true, transport:'network', ready:true, host_utc_valid:true, phase:'idle', session_phase:'ready', now_ms:'10000', status_observed_ms:'1000',
                remote:{output_active:false}, owns:false, network:{hostname:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local',port:18443,expected_identity:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local',resolved_address:'192.0.2.27',authenticated_identity:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local',state:'ready',observed_ms:'9000'} };
            window.fetch = async (url, options = {}) => {
                window.__requests.push({url,options});
                if (String(url).endsWith('/member/management')) {
                    const body = options.body ? JSON.parse(options.body) : null;
                    if (body?.action === 'connect' && window.__consumerBusy) return {ok:false,status:window.__consumerBusy==='rate'?429:409,json:async()=>({error:{code:({capacity:'radio_session_capacity',pending:'radio_login_pending',rate:'radio_login_rate_limited'})[window.__consumerBusy]||'radio_session_busy'}})};
                    if (body?.action === 'connect') window.__management = {carrier:body.carrier,host:body.host,connected:true};
                    if (body?.action === 'disconnect') window.__management = {carrier:'disconnected',host:'',connected:false};
                    return {ok:true,headers:{get:()=>''},json:async()=>window.__management || {carrier:'engineering_mtls',host:'',connected:false}};
                }
                if (String(url).endsWith('/member/application')) {
                    if(window.__resourceState==='unsupported')return {ok:false,status:409,json:async()=>({error:{code:'shared_resources_unavailable'}})};
                    return {ok:true,headers:{get:()=> '\"resource-r1\"'},json:async()=>structuredClone(window.__resourceFixtures[window.__resourceState])};
                }
                if (String(url).endsWith('/member/pins/validate'))return {ok:true,headers:{get:()=>''},json:async()=>({valid:true,operational_supported:true,applied:false})};
                if (String(url).endsWith('/member/station') || String(url).endsWith('/member/hardware'))return {ok:true,headers:{get:()=> '\"resource-r2\"'},json:async()=>({pending_restart:true})};
                if (String(url).endsWith('/host/devices')) return {ok:true,headers:{get:()=> '"catalog-r1"'},json:async()=>({scope:'wsprrypi-wtp-catalog/1',version:1,active_id:'c'.repeat(32),active_unmatched:false,profiles:[{id:'c'.repeat(32),name:'Bench Pico',method:'manual',discovery_id:'',legacy_unverified:false,settings:{Transport:'network',Hostname:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local','TCP Port':18443,'TLS Server Identity':'pico.local','TLS CA File':'/etc/wsprrypi/pico/ca.crt','TLS Client Certificate':'/etc/wsprrypi/pico/client.crt','TLS Client Key':'/etc/wsprrypi/pico/client.key',Endpoint:'', 'USB Serial':'','USB Vendor ID':0,'USB Product ID':0,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000,'Allow Frequency Adjustment':false}}]})};
                if (String(url).endsWith('/host/discovery')) return {ok:true,json:async()=>({available:true,reason:'',candidates:[{id:'service-nearby',interface:2,protocol:0,instance:'Nearby Pico',domain:'local',target:'nearby.local',port:40123,binding:'plain',state:'online',first_seen_ms:1,last_seen_ms:2}]})};
                if (String(url).endsWith('/status')) {
                    if (window.__statusFailure) throw new Error('Connection interrupted');
                    return {ok:true, json:async()=>({host:window.__snapshot})};
                }
                if (String(url).endsWith('/jobs')) { window.__snapshot.owns=false; window.__snapshot.job_id=''; return {ok:true,json:async()=>({ok:true,result:{cleanup_ok:true}})}; }
                if (String(url).endsWith('/network')) return {ok:true,headers:{get:()=> '\"network-r1\"'},json:async()=>({enabled:true,ipv4:'192.0.2.27',configured_hostname:window.__snapshot.network.hostname,advertised_hostname:window.__discoveryState==='active'?window.__snapshot.network.hostname:'',mdns_state:window.__discoveryState,mdns_reason:window.__discoveryState==='conflict'?'name_conflict':''})};
                if (window.__remoteFailure) return {ok:false,status:412,json:async()=>({error:{code:'revision_conflict'}})};
                if (String(url).endsWith('/member/configuration')) {
                    if (options.method === 'POST' && window.__mutationFailure) return {ok:false,status:window.__mutationFailure==='unsupported'?409:503,json:async()=>({error:{code:window.__mutationFailure==='unsupported'?'unsupported_configuration_updates':'management_outcome_unknown'}})};
                    return {ok:true,headers:{get:()=> '\"remote-r1\"'},json:async()=>({schema:'wsprrypi-pico-settings/1',target:{scope:'member',device_id:'a'.repeat(32),boot_id:'1'.repeat(32)},update_strategy:'merge',reboot_required:false,config:{version:1,enabled:false,station:{callsign:'AA0NT',locator:'EM18',power_dbm:20},wifi:{ssid:'Bench Wi-Fi',password:null,ntp_ipv4:'192.0.2.1'},schedules:[{period_s:120,phase_s:0}]}})};
                }
                return {ok:true,headers:{get:()=> '"remote-r1"'},json:async()=>({config:{version:1,enabled:false,station:{callsign:'AA0NT',locator:'EM18',power_dbm:20},wifi:{ssid:'Bench Wi-Fi',password:null,ntp_ipv4:'192.0.2.1'},schedules:[{period_s:120,phase_s:0}]}})};
            };
            window.WtpUi.populate({Transport:'network',Hostname:window.__snapshot.network.hostname,'TCP Port':18443,'Device ID':'a'.repeat(32),'TLS CA File':'/etc/wsprrypi/pico/ca.crt','TLS Client Certificate':'/etc/wsprrypi/pico/client.crt','TLS Client Key':'/etc/wsprrypi/pico/client.key','TLS Server Identity':'','Start Uncertainty ns':1000000,'Allow Frequency Adjustment':false,Endpoint:'/dev/preserved','USB Serial':'00001234','USB Vendor ID':51966,'USB Product ID':16402});

            window.WtpUi.select(true);
            document.getElementById('fleet-tab').click();
        })()`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-network-state").textContent.includes("192.0.2.27")'), 'network snapshot');
        assert.equal(await evaluate('document.getElementById("fleet-tab").classList.contains("active")'), true);
        await waitFor(async () => await evaluate('document.getElementById("fleet-device").options.length === 2'), 'Fleet catalog');
        assert.equal(await evaluate('document.getElementById("fleet-device").value'), 'known:'+'c'.repeat(32));
        await evaluate(`(() => { const selector=document.getElementById('fleet-device'); selector.value='nearby:service-nearby'; selector.dispatchEvent(new Event('change',{bubbles:true})); document.getElementById('fleet-add').click(); document.getElementById('fleet-name').value='Nearby draft'; })()`);
        assert.equal(await evaluate('document.getElementById("fleet-host").value'), 'nearby.local');
        assert.equal(await evaluate('document.getElementById("fleet-port").value'), '40123');
        assert.equal(await evaluate('window.WtpUi.read().Hostname'), 'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local');
        assert.equal(await evaluate('window.__requests.filter(r => r.url.endsWith("/host/devices") && r.options.method === "POST").length'), 0);
        for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
            await client.send('Emulation.setDeviceMetricsOverride', {...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            await evaluate(`document.getElementById('fleet-device').scrollIntoView({block:'center',behavior:'instant'});`);
            await new Promise(resolve=>setTimeout(resolve,200));
            await capture(`${viewport.name}-fleet-selector.png`, {format:'png'});
            assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'), 'Fleet horizontal overflow');
            await evaluate('document.getElementById("fleet-binding").scrollIntoView({block:"center",behavior:"instant"})');
            await capture(`${viewport.name}-fleet-editor.png`, {format:'png'});
        }
        await client.send('Emulation.setDeviceMetricsOverride', {width:1280,height:900,deviceScaleFactor:1,mobile:false});
        await evaluate(`document.documentElement.style.zoom='2'; document.getElementById('fleet-device').scrollIntoView({block:'center',behavior:'instant'});`);
        assert(await evaluate('document.documentElement.scrollWidth <= innerWidth + 1'), 'Fleet 200% text/layout overflow');
        await capture('desktop-fleet-zoom.png', {format:'png'});
        await evaluate(`document.documentElement.style.zoom='';`);
        await evaluate(`document.getElementById('fleet-editor-cancel').click()`);
        assert.equal(await evaluate('window.WtpUi.validate()'), true);
        await evaluate(`(() => {
            window.__snapshot.transport='network_plain';
            window.__snapshot.network.security='plain_lan';
            document.getElementById('wtp_transport').value='network_plain';
            document.getElementById('wtp_transport').dispatchEvent(new Event('change',{bubbles:true}));
            document.getElementById('wtp_tcp_port').value='31417';
            document.getElementById('wtp_device').value='';
        })()`);
        assert.equal(await evaluate('window.WtpUi.validate()'), true);
        assert.equal(await evaluate('document.querySelector("[data-wtp-security=tls]").hidden'), true);
        assert.equal(await evaluate('document.querySelector("[data-wtp-transport=network]").hidden'), false);
        assert.equal(await evaluate('document.getElementById("wtp-device-label").textContent.includes("optional")'), true);
        assert.equal(await evaluate('document.getElementById("wtp-management").hidden'), false);
        for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
            await client.send('Emulation.setDeviceMetricsOverride', {...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            await new Promise(resolve=>setTimeout(resolve,200));
            await evaluate('document.getElementById("wtp_transport").scrollIntoView({block:"center",behavior:"instant"})');
            const transportVisible = await evaluate('(() => { const r=document.getElementById("wtp_transport").getBoundingClientRect(); return {height:r.height,top:r.top,bottom:r.bottom,viewport:window.innerHeight}; })()');
            assert(transportVisible.height > 0 && transportVisible.top >= 0 && transportVisible.bottom <= transportVisible.viewport, `Plain LAN selector visible: ${JSON.stringify(transportVisible)}`);
            await capture(`${viewport.name}-plain-lan.png`, {format:'png'});
            assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'), 'Plain LAN horizontal overflow');
        }
        await evaluate(`(() => {
            window.__snapshot.transport='network';
            window.__snapshot.network.security='tls';
            document.getElementById('wtp_transport').value='network';
            document.getElementById('wtp_transport').dispatchEvent(new Event('change',{bubbles:true}));
            document.getElementById('wtp_tcp_port').value='18443';
            document.getElementById('wtp_device').value='a'.repeat(32);
        })()`);
        await evaluate(`document.getElementById('wtp_transport').value='usb'; document.getElementById('wtp_transport').dispatchEvent(new Event('change',{bubbles:true}));`);
        assert.equal(await evaluate('window.WtpUi.read().Hostname'), 'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local');
        await evaluate(`document.getElementById('wtp_transport').value='network'; document.getElementById('wtp_transport').dispatchEvent(new Event('change',{bubbles:true})); document.getElementById('wtp-management').open=true; document.getElementById('wtp-remote-load').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-remote-callsign").value === "AA0NT"'), 'standalone settings');
        await evaluate(`document.getElementById('wtp-remote-callsign').value='MYDRAFT'; window.__remoteFailure=true; document.getElementById('wtp-remote-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-management-feedback").textContent.includes("changed elsewhere")'), 'revision feedback');
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'), 'MYDRAFT');
        assert.equal(await evaluate('document.getElementById("wtp-remote-password").value'), '');
        await evaluate(`window.__remoteFailure=false; document.getElementById('wtp-remote-load').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-management-feedback").textContent.includes("draft and password entry are preserved")'), 'conflict readback');
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'),'MYDRAFT');
        await evaluate(`document.getElementById('wtp-remote-password').value='unsaved-secret'; document.getElementById('wtp-schedules-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-management-feedback").textContent.includes("schedules saved")'), 'schedule save');
        assert.equal(await evaluate('document.getElementById("wtp-remote-password").value'),'unsaved-secret');
        for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
            await client.send('Emulation.setDeviceMetricsOverride', {...viewport, deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            await waitFor(async () => await evaluate('(() => { const tab=document.getElementById("fleet-tab").getBoundingClientRect(); const list=document.getElementById("configTabs").getBoundingClientRect(); return tab.left >= list.left - 1 && tab.right <= list.right + 1; })()'), 'Fleet tab in view');
            for (const section of ['wtp-remote-password','wtp-management-feedback']) {
                await evaluate(`document.getElementById('${section}').scrollIntoView({block:'center',behavior:'instant'});`);
                await new Promise(resolve=>setTimeout(resolve,200));
                await capture(`${viewport.name}-schedule-${section}.png`, {format:'png'});
            }
        }
        for (const state of ['entry', 'connected', 'busy', 'capacity', 'pending', 'rate']) {
            await evaluate(`window.__consumerBusy=${JSON.stringify(['busy','capacity','pending','rate'].includes(state)?state:false)}; window.WtpManagement.setAvailability(true,true); document.getElementById('wtp-management-carrier').value='consumer_station'; document.getElementById('wtp-management-carrier').dispatchEvent(new Event('change')); document.getElementById('wtp-management-host').value='192.168.1.53';`);
            if (state !== 'entry') {
                await evaluate(`document.getElementById('wtp-management-password').value='synthetic-test-password'; document.getElementById('wtp-management-connect').click();`);
                await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'consumer login result');
                assert.equal(await evaluate('document.getElementById("wtp-management-password").value'),'');
            }
            for (const theme of ['light','dark']) for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                await evaluate(`applyThemePreference('${theme}'); document.getElementById('wtp-management-carrier').scrollIntoView({block:'start',behavior:'instant'}); window.scrollBy(0,-190);`);
                // Allow the resized layout to paint before framing the scoped feedback.
                await evaluate(`new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
                if (['busy','capacity','pending','rate'].includes(state))
                    await evaluate(`document.getElementById('wtp-management-connection-feedback').scrollIntoView({block:'center',behavior:'instant'}); new Promise(resolve=>requestAnimationFrame(resolve))`);
                await capture(`${viewport.name}-${theme}-consumer-${state}.png`,{format:'png'});
                assert(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'consumer management overflow');
            }
            if (state === 'connected') {
                await evaluate(`document.getElementById('wtp-management-disconnect').click();`);
                await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'consumer disconnect');
            }
        }
        await evaluate(`window.__consumerBusy=false; window.__management={carrier:'engineering_mtls',host:'',connected:false}; document.getElementById('wtp-management-state').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'restore engineering management');
        // One batched P15.4 visual pass: real emitted resource fixtures, with
        // mocked browser transport only. Actual carrier evidence runs separately.
        for (const state of ['loaded','consumer','fault','unsupported']) {
            await evaluate(`window.__resourceState='${state}'; window.WtpManagement.setAvailability(true,true); document.getElementById('wtp-member-load').click();`);
            await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'shared resource load');
            if(state==='loaded') {
                assert.equal(await evaluate('document.getElementById("wtp-member-station-save").disabled'),false);
                assert.equal(await evaluate(`(()=>{
                    const original=window.scheduleAutosave; let calls=0; window.scheduleAutosave=()=>{calls++;};
                    for(const id of ['wtp-member-callsign','wtp-member-rf','wtp-remote-password','wtp-management-carrier']) {
                        const e=document.getElementById(id);e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));
                    }
                    const memberCalls=calls;document.getElementById('wtp_hostname').dispatchEvent(new Event('input',{bubbles:true}));
                    window.scheduleAutosave=original;return memberCalls===0&&calls>0;
                })()`),true,'member draft changes must never schedule host autosave');
                const before=await evaluate('window.__requests.filter(r=>r.options.method==="PUT").length');
                await evaluate(`document.getElementById('wtp-member-validate').click()`);
                await waitFor(async()=>await evaluate('document.getElementById("wtp-member-pin-feedback").textContent.includes("Preview only")'),'pin preview');
                assert.equal(await evaluate('window.__requests.filter(r=>r.options.method==="PUT").length'),before);
                assert.equal(await evaluate('document.getElementById("wtp-member-hardware-save").disabled'),false);
            }
            if(state==='consumer') {
                assert.equal(await evaluate('document.getElementById("wtp-member-station-save").disabled'),true);
                assert.match(await evaluate('document.getElementById("wtp-member-active").textContent'),/Unknown/);
                assert.match(await evaluate('document.getElementById("wtp-member-restart").textContent'),/latched/);
            }
            if(state==='fault'||state==='unsupported') assert.equal(await evaluate('document.getElementById("wtp-member-hardware-save").disabled'),true);
            for(const theme of ['light','dark'])for(const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                await evaluate(`applyThemePreference('${theme}'); document.getElementById('wtp-member-heading').scrollIntoView({block:'start',behavior:'instant'});`);
                const rect=await evaluate(`(()=>{const r=document.getElementById('wtp-member-heading').closest('section').getBoundingClientRect();return {x:Math.max(0,r.x+scrollX),y:Math.max(0,r.y+scrollY),width:r.width,height:r.height,scale:1};})()`);
                await capture(`${viewport.name}-${theme}-resources-${state}.png`,{format:'png',captureBeyondViewport:true,clip:rect});
                assert(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'member resource overflow');
                assert(await evaluate('[...document.querySelectorAll("[data-wtp-resource]")].every(e=>document.querySelector(`label[for="${e.id}"]`))'),'member control labels');
                assert.equal(await evaluate('getComputedStyle(document.getElementById("wtp-member-feedback")).fontSize'),'14px');
            }
        }
        for (const state of ['saved', 'stale', 'unsupported', 'ambiguous', 'busy']) {
            await evaluate(`window.__remoteFailure=false; window.__mutationFailure=''; window.WtpManagement.setAvailability(true); document.getElementById('wtp-remote-load').click();`);
            await waitFor(async () => await evaluate('!document.getElementById("wtp-remote-save").disabled'), 'settings readback');
            await evaluate(`document.getElementById('wtp-remote-callsign').value='K1ABC'; window.__remoteFailure=${state === 'stale'}; window.__mutationFailure='${['unsupported','ambiguous'].includes(state) ? state : ''}'; ${state === 'busy' ? 'window.WtpManagement.setAvailability(false)' : "document.getElementById('wtp-remote-save').click()"};`);
            await waitFor(async () => await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy") === "false"'), 'settings result');
            for (const theme of ['light','dark']) for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                await evaluate(`applyThemePreference('${theme}'); document.getElementById('wtp-management-feedback').scrollIntoView({block:'center',behavior:'instant'});`);
                await waitFor(async () => await evaluate(`(() => {
                    const element=document.getElementById('wtp-management-feedback');
                    element.scrollIntoView({block:'center',behavior:'instant'});
                    const r=element.getBoundingClientRect(); return r.height>0 && r.top>=0 && r.bottom<=innerHeight-32;
                })()`), 'settings feedback in viewport');
                await new Promise(resolve=>setTimeout(resolve,200));
                await capture(`${viewport.name}-${theme}-settings-${state}.png`, {format:'png'});
                assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'),'settings horizontal overflow');
            }
        }
        await evaluate(`window.__remoteFailure=false; window.__mutationFailure=''; window.WtpManagement.setAvailability(true); document.getElementById('wtp-remote-load').click();`);
        await waitFor(async () => await evaluate('!document.getElementById("wtp-remote-save").disabled'), 'final settings readback');
        await evaluate(`document.getElementById('wtp-remote-password').value='unsaved-secret';`);
        for (const discovery of ['active', 'conflict', 'failed']) {
            await evaluate(`window.__discoveryState='${discovery}'; document.getElementById('wtp-network-load').click();`);
            await waitFor(async () => await evaluate(`document.getElementById('wtp-management-feedback').textContent.includes('Discovery: ${discovery}.')`), 'discovery feedback');
            assert.equal(await evaluate('document.getElementById("wtp-remote-password").value'), 'unsaved-secret');
            for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride', {...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                await evaluate(`document.getElementById('wtp-management-feedback').scrollIntoView({block:'center',behavior:'instant'});`);
                await capture(`${viewport.name}-discovery-${discovery}.png`, {format:'png'});
                assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'), 'discovery horizontal overflow');
            }
        }
        await evaluate(`document.getElementById('wtp-remote-callsign').value='MYDRAFT'; window.__snapshot.owns=true; window.__snapshot.job_id='b'.repeat(32); window.WtpUi.select(true);`);
        await waitFor(async () => await evaluate('!document.getElementById("wtp-cancel").disabled'), 'owned cancellation');
        await evaluate(`document.getElementById('wtp-cancel').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-feedback").textContent.includes("Cleanup confirmed")'), 'shared cancellation');
        assert.equal(await evaluate('JSON.parse(window.__requests.find(r=>r.url.endsWith("/jobs")).options.body).operation'), 'ABORT');
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'), 'MYDRAFT');
        await evaluate(`window.__remoteFailure=true; document.getElementById('wtp-remote-password').value='';`);
        await waitFor(async () => await evaluate('!document.getElementById("wtp-remote-save").disabled'), 'idle management restored');
        await evaluate(`document.getElementById('wtp-remote-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-management-feedback").textContent.includes("changed elsewhere")'), 'conflict capture');
        for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
            await client.send('Emulation.setDeviceMetricsOverride', {...viewport, deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            await evaluate('window.scrollTo({top:0,behavior:"instant"})');
            await new Promise(resolve=>setTimeout(resolve,500));
            const layout=await client.send('Page.getLayoutMetrics');
            await capture(`${viewport.name}-full.png`, {format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width:viewport.width,height:layout.cssContentSize.height,scale:1}});
            for (const section of ['wtp_transport','wtp_hostname','wtp-status-heading','wtp-management','wtp-management-feedback']) {
                await evaluate(`document.getElementById('${section}').scrollIntoView({block:'center',behavior:'instant'});`);
                await new Promise(resolve=>setTimeout(resolve,150));
                await capture(`${viewport.name}-${section}.png`, {format:'png'});
            }
            assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'), 'horizontal overflow');
        }
        await evaluate(`document.getElementById('wtp_hostname').value='DrAfT.LoCaL.'; window.__statusFailure=true;`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-feedback").textContent.includes("Connection interrupted")'), 'status failure');
        assert.equal(await evaluate('window.WtpUi.read().Hostname'), 'DrAfT.LoCaL.');
        assert.equal(await evaluate('document.getElementById("wtp-output").textContent'), 'Unknown');
        assert.equal(await evaluate('document.getElementById("wtp-recover").disabled'),true);
        await evaluate(`document.getElementById('wtp-status-heading').scrollIntoView({block:'start',behavior:'instant'}); window.scrollBy(0,-140);`);
        assert.equal(await evaluate('document.getElementById("wtp-remote-load").disabled'),true);
        await new Promise(resolve=>setTimeout(resolve,200));
        await capture('mobile-status-failure.png', {format:'png'});
        await evaluate('document.getElementById("transmitter-hardware-tab").click()');
        await waitFor(async () => await evaluate('document.getElementById("transmitter-hardware-tab").classList.contains("active")'), 'Transmitter tab after leaving Fleet');
        assert.equal(await evaluate('document.getElementById("fleet-tab-item").hidden || document.getElementById("fleet-pane").hidden'), false);
        assert.equal(await evaluate('window.WtpUi.read().Hostname'), 'DrAfT.LoCaL.');
        assert.equal(client.dialogError, null);
        assert(client.dialogs.length > 0, 'saved-state review must show a real browser confirmation');
        console.log(`Accepted ${client.dialogs.length} expected saved-state review confirmations`);
        console.log('Rendered desktop/mobile: transport selection, preserved inactive settings, management revision failure, status failure and drafts passed');
        console.log(captureScreenshots ? 'Screenshots: '+output : 'Screenshot capture disabled; existing visual evidence retained.');
    } catch (error) {
        failure = new Error(`${error.message}\n${childDiagnostic(chrome || php)}`, { cause: error });
    } finally {
        const errors = failure ? [failure] : [];
        if (client && chrome?.exitCode === null) await Promise.race([client.send('Browser.close').catch(() => {}),new Promise(resolve=>setTimeout(resolve,1000))]);
        for(const cleanup of [()=>client?.close(),()=>stopIsolatedProcess(chrome),()=>stopChild(php),
            ()=>fs.rmSync(profileDir,{recursive:true,force:true,maxRetries:10,retryDelay:100})]) {
            try { await cleanup(); } catch(error) { if(!errors.includes(error)) errors.push(error); }
        }
        if(errors.length) throw new AggregateError(errors,'Network browser workflow or cleanup failed');
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
