// SPDX-License-Identifier: MIT
// Opt-in rendered recovery workflow with real shipped UI and mocked HTTP responses.
// Generate the source-paired resource JSON with wtp-config-interop-test first.
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
        this.pending = new Map(); this.dialogs=[]; this.acceptDialogs=true;
        this.socket.on("message", (raw) => {
            const message = JSON.parse(raw); if(message.method==='Page.javascriptDialogOpening'){this.dialogs.push(message.params.message);void this.send('Page.handleJavaScriptDialog',{accept:this.acceptDialogs});return;}
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
    const profileDir = path.join(require("node:os").tmpdir(), `wsprrypi-recovery-profile-${process.pid}`);
    const output = path.resolve(process.argv[3] || path.join(require('node:os').tmpdir(), `wsprrypi-recovery-ui-${process.pid}`));
    fs.mkdirSync(output, { recursive: true });
    const captureScreenshots = process.env.WSPRRYPI_UI_CAPTURE !== '0';
    const resourceBuild=path.resolve(process.argv[2] || path.join(UI_ROOT,'../src/build/wtp-network'));
    const resourceFixtures=require('./wtp_recovery_resource_fixtures.js').loadResourceFixtures(resourceBuild);
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
        const evaluate = async expression => {
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
                    if (body?.action === 'inspect_logins') return {ok:true,headers:{get:()=>''},json:async()=>({schema:'consumer-recovery/1',device_id:'a'.repeat(32),boot_id:'1'.repeat(32),generation:'3',authority:'192.168.1.53',interface:'station',proof_ms:5000,selection_id:'f'.repeat(32),sessions:[{selector:'b'.repeat(32),state:'active'}]})};
                    if (body?.action === 'retire_login') return window.__retireFailure ? {ok:false,status:503,json:async()=>({error:{code:'retirement_unconfirmed_'+ 'x'.repeat(280)}})} : {ok:true,headers:{get:()=>''},json:async()=>({retired:true,present:true,selection_id:'e'.repeat(32)})};
                    if (body?.action === 'connect' && window.__consumerBusy) return {ok:false,status:window.__consumerBusy==='rate'?429:409,json:async()=>({error:{code:({capacity:'radio_session_capacity',pending:'radio_login_pending',rate:'radio_login_rate_limited'})[window.__consumerBusy]||'radio_session_busy'}})};
                    if (body?.action === 'engineering_mtls') window.__management = {carrier:'engineering_mtls',host:'',connected:false};
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
            window.WtpUi.developmentControlsVisible = true;
            window.WtpUi.select(true);
            document.getElementById('fleet-tab').click();
        })()`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-network-state").textContent.includes("192.0.2.27")'), 'network snapshot');

        await client.send('Page.enable');
        await evaluate(`document.getElementById('wtp-management').open=true;document.getElementById('wtp-management-carrier').value='consumer_station';document.getElementById('wtp-management-carrier').dispatchEvent(new Event('change'));document.getElementById('wtp-management-host').value='192.168.1.53';document.getElementById('wtp-management-password').value='synthetic-test-password';document.getElementById('wtp-management-connect').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'consumer connected');
        await evaluate(`document.getElementById('wtp-remote-load').click();`);
        await waitFor(async()=>await evaluate('!document.getElementById("wtp-remote-save").disabled'),'settings loaded');
        await evaluate(`document.getElementById('wtp-remote-callsign').value='MYDRAFT';window.__mutationFailure='ambiguous';document.getElementById('wtp-remote-save').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'uncertain save');
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'),'MYDRAFT');
        assert.equal(await evaluate('document.getElementById("wtp-remote-save").disabled'),true);
        const saves=await evaluate('window.__requests.filter(r=>r.url.endsWith("/member/configuration")&&r.options.method==="POST").length');assert.equal(saves,1);
        await evaluate(`window.__mutationFailure='';document.getElementById('wtp-remote-load').click();`);
        await waitFor(async()=>await evaluate('!document.getElementById("wtp-remote-save").disabled'),'fresh readback');
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'),'MYDRAFT');
        assert.match(await evaluate('document.getElementById("wtp-remote-callsign-hint").textContent'),/Saved: AA0NT/);
        client.acceptDialogs=false;
        await evaluate(`document.getElementById('wtp-remote-save').click();`);await waitFor(()=>client.dialogs.length===1,'review confirmation');
        assert.equal(await evaluate('window.__requests.filter(r=>r.url.endsWith("/member/configuration")&&r.options.method==="POST").length'),saves);
        for(const theme of ['light','dark'])for(const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]){
          await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
          await evaluate(`new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
          await evaluate(`applyThemePreference('${theme}');document.getElementById('wtp-remote-callsign').scrollIntoView({block:'center',behavior:'instant'});new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
          assert(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'readback overflow');
          assert(await evaluate('document.getElementById("wtp-remote-callsign").getBoundingClientRect().top>0 && document.getElementById("wtp-remote-callsign").getBoundingClientRect().bottom<innerHeight'),'readback control visible');
          await capture(`${viewport.name}-${theme}-readback.png`);
        }
        await evaluate(`document.getElementById('wtp-management-recovery').open=true;document.getElementById('wtp-management-inspect').focus();`);
        await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Enter',code:'Enter',windowsVirtualKeyCode:13,text:'\r'});await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Enter',code:'Enter',windowsVirtualKeyCode:13});
        await waitFor(async()=>await evaluate('document.getElementById("wtp-management-target").options.length===2'),'keyboard inspection');
        await evaluate(`document.getElementById('wtp-management-target').value='b'.repeat(32);document.getElementById('wtp-management-target').dispatchEvent(new Event('change'));document.getElementById('wtp-management-password').value='synthetic-test-password';document.getElementById('wtp-management-retire').click();`);
        await waitFor(()=>client.dialogs.length===2,'retirement decline');
        assert.equal(await evaluate('window.__requests.filter(r=>r.options.body&&JSON.parse(r.options.body).action==="retire_login").length'),0);
        client.acceptDialogs=true;
        await evaluate(`window.__retireFailure=true;document.getElementById('wtp-management-retire').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'retirement failure');
        assert.equal(await evaluate('document.getElementById("wtp-management-password").value'),'');
        assert.equal(await evaluate('document.getElementById("wtp-remote-save").disabled'),true);
        for(const theme of ['light','dark'])for(const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]){
          await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
          await evaluate(`new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
          await evaluate(`applyThemePreference('${theme}');document.getElementById('wtp-management-retirement-status').scrollIntoView({block:'center',behavior:'instant'});new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
          assert(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'retirement long error overflow');
          assert(await evaluate('[...document.querySelectorAll("#wtp-management-recovery select")].every(e=>document.querySelector(`label[for="${e.id}"]`))'),'recovery labels');
          await capture(`${viewport.name}-${theme}-retirement.png`);
        }
        await evaluate(`window.__retireFailure=false;document.getElementById('wtp-management-password').value='synthetic-test-password';document.getElementById('wtp-management-retire').click();`);
        await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'retirement success');
        assert.match(await evaluate('document.getElementById("wtp-management-retirement-status").textContent'),/retired or already absent/);
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'),'MYDRAFT');
        assert.equal(await evaluate('window.__requests.filter(r=>r.url.endsWith("/member/configuration")&&r.options.method==="POST").length'),1);
        {
          await evaluate(`document.getElementById('wtp-management-carrier').value='engineering_mtls';document.getElementById('wtp-management-carrier').dispatchEvent(new Event('change'));document.getElementById('wtp-management-connect').click();`);
          await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'engineering selection');
          // Reconcile the management selection invalidated by retirement before
          // loading station revisions, following the operator workflow.
          await evaluate(`window.WtpManagement.setAvailability(true,true)`);
          await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'fresh engineering management state');
          const scenarios=resourceFixtures.plans.length ? resourceFixtures.plans : [['legacy',resourceFixtures.loaded]];
          for (const [scenario,saved] of scenarios) {
            await evaluate(`window.__resourceFixtures.loaded=${JSON.stringify(saved)};window.__resourceState='loaded';document.getElementById('wtp-member-load').click();`);
            await waitFor(async()=>await evaluate('document.getElementById("wtp-management").getAttribute("aria-busy")==="false"'),'versioned station readback');
            assert.match(await evaluate('document.getElementById("wtp-member-station-feedback").textContent'),scenario==='legacy'?/Type 1 station writes only/:/Saved plan:/);
            const station=saved.application.station.station;
            await evaluate(`document.getElementById('wtp-member-callsign').value=${JSON.stringify(station.callsign)};document.getElementById('wtp-member-locator').value=${JSON.stringify(station.locator)};document.getElementById('wtp-member-callsign').dispatchEvent(new Event('input'));document.getElementById('wtp-member-callsign').focus();`);
            await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
            await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
            assert.equal(await evaluate('document.activeElement.id'),'wtp-member-locator');
            if(['type3','w1a-type3'].includes(scenario)) {
              await evaluate(`document.getElementById('wtp-member-locator').value='EM18';document.getElementById('wtp-member-locator').dispatchEvent(new Event('input'));`);
              assert.match(await evaluate('document.getElementById("wtp-member-station-feedback").textContent'),/six-character/);
              await evaluate(`document.getElementById('wtp-member-locator').value='EM18IG';document.getElementById('wtp-member-locator').dispatchEvent(new Event('input'));`);
            }
            assert.match(await evaluate('document.getElementById("wtp-member-station-feedback").textContent'),scenario==='ambiguity'?/ambiguous/:saved.application.wspr_plan?.frame_types.length===2?/Type 2 then Type 3/:['type3','w1a-type3'].includes(scenario)?/Type 3/:scenario==='type2'?/Type 2/:/Type 1/);
            for (const theme of ['light','dark']) for(const viewport of [{name:'desktop',width:1440,height:1100},{name:'mobile',width:390,height:844}]) for(const textScale of [100,200]) {
              await evaluate(`document.documentElement.style.fontSize='${textScale}%'`);
              await client.send('Emulation.setDeviceMetricsOverride',{...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
              await evaluate(`applyThemePreference('${theme}');new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
              await evaluate(`document.getElementById('wtp-member-callsign').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-160);new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))`);
              assert(await evaluate('document.getElementById("wtp-member-callsign").getBoundingClientRect().top>=100'),'station field framing');
              assert(await evaluate('document.documentElement.scrollWidth<=innerWidth+1'),'WSPR plan feedback overflow');
              assert.match(await evaluate('document.getElementById("wtp-member-station-hint").textContent'),scenario==='legacy'?/Type 1 station writes only/:/automatically selects Type 1, Type 2 or Type 3/);
              assert.equal(await evaluate('document.getElementById("wtp-member-locator").getAttribute("aria-describedby")'),'wtp-member-station-hint');
              const bounds=await evaluate(`(() => { const top=document.getElementById('wtp-member-station-hint').getBoundingClientRect().top+scrollY;const bottom=document.getElementById('wtp-member-station-feedback').getBoundingClientRect().bottom+scrollY;return {x:0,y:Math.max(0,top-160),width:innerWidth,height:bottom-Math.max(0,top-160)+320,scale:1}; })()`);
              await capture(`${viewport.name}-${theme}-${textScale}-wspr-${scenario}.png`,{format:'png',captureBeyondViewport:true,clip:bounds});
            }
          }
          await evaluate(`document.documentElement.style.fontSize='100%';document.getElementById('wtp-member-callsign').value='BAD//CALL';document.getElementById('wtp-member-callsign').dispatchEvent(new Event('input'));`);
          assert.match(await evaluate('document.getElementById("wtp-member-station-feedback").textContent'),resourceFixtures.plans.length?/Pico validates encoding before saving/:/Type 1 station writes only/);
          assert.equal(await evaluate('document.getElementById("wtp-member-callsign").value'),'BAD//CALL');
          console.log(resourceFixtures.plans.length?'All eight versioned station plans, unsupported draft, keyboard, desktop/mobile light/dark and 200% text passed':'Historical Type 1 negotiated hint/refusal, preserved draft, keyboard, desktop/mobile light/dark and 200% text passed');
        }
        console.log(JSON.stringify({source:UI_ROOT,browser:(await client.send('Browser.getVersion')).product,readback_preserves_draft:true,decline_sends_nothing:true,keyboard_inspection:true,retirement_error_and_success:true,password_scrubbed:true,no_mutation_replay:true,screenshots:captureScreenshots?(8+Math.max(1,resourceFixtures.plans.length)*8):0,fixture_build:resourceBuild,output}));
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
if (process.env.WSPRRYPI_RUN_RECOVERY_BROWSER === "1") main().catch(error=>{console.error(error);process.exitCode=1;});
else console.log("Recovery browser fixture skipped; set WSPRRYPI_RUN_RECOVERY_BROWSER=1 to opt in.");
