// SPDX-License-Identifier: MIT
// Hardware-free rendered WTP network workflow with mocked HTTP responses.
"use strict";

const assert = require("node:assert/strict");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");
const { spawn } = require("node:child_process");
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
        this.socket.on("message", (raw) => {
            const message = JSON.parse(raw);
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
    const php = spawn('php', ['-S', `127.0.0.1:${phpPort}`, '-t', 'data'], { cwd: UI_ROOT, stdio: 'ignore' });
    const url = `http://127.0.0.1:${phpPort}/index.php?page=config`;
    const profileDir = `/tmp/wsprrypi-network-ui-${process.pid}`;
    const output = path.resolve(UI_ROOT, '../src/build/wtp-network/ui');
    fs.mkdirSync(output, { recursive: true });
    let chrome, client;
    try {
        await waitFor(async () => await getStatus(url) === 200, 'PHP fixture');
        chrome = spawn(process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'chromium'),
            ['--headless', '--no-sandbox', '--disable-gpu', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profileDir}`, url], { stdio: 'ignore' });
        const page = await waitFor(async () => (await getJson(`http://127.0.0.1:${debugPort}/json`)).find(p => p.type === 'page'), 'browser');
        client = new CdpClient(page.webSocketDebuggerUrl); await client.open();
        const evaluate = async expression => {
            const result = await client.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
            if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
            return result.result.value;
        };
        await waitFor(async () => await evaluate('document.readyState === "complete" && !!window.WtpUi && !!window.WtpManagement'), 'WTP scripts');
        await evaluate(`(() => {
            configAutosaveSuspended = true;
            clearPendingPopulateConfigRetry(); clearWebSocketReconnectTimer();
            backendCurrentlyConnected = true; websocketCurrentlyConnected = true;
            syncConnectionAlert(); clearBackendStatus('runtime'); clearConfigLoadFailureState();
            const style = document.createElement('style'); style.textContent='* { scroll-behavior: auto !important; }'; document.head.append(style);
            window.__requests = [];
            window.__remoteFailure = false;
            window.__statusFailure = false;
            window.__discoveryState = "active";
            window.__snapshot = { selected:true, transport:'network', ready:true, host_utc_valid:true, phase:'idle', session_phase:'ready', now_ms:'10000', status_observed_ms:'1000',
                remote:{output_active:false}, owns:false, network:{hostname:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local',port:18443,expected_identity:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local',resolved_address:'192.0.2.27',authenticated_identity:'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local',state:'ready',observed_ms:'9000'} };
            window.fetch = async (url, options = {}) => {
                window.__requests.push({url,options});
                if (String(url).endsWith('/status')) {
                    if (window.__statusFailure) throw new Error('Connection interrupted');
                    return {ok:true, json:async()=>({host:window.__snapshot})};
                }
                if (String(url).endsWith('/jobs')) { window.__snapshot.owns=false; window.__snapshot.job_id=''; return {ok:true,json:async()=>({ok:true,result:{cleanup_ok:true}})}; }
                if (String(url).endsWith('/network')) return {ok:true,headers:{get:()=> '\"network-r1\"'},json:async()=>({enabled:true,ipv4:'192.0.2.27',configured_hostname:window.__snapshot.network.hostname,advertised_hostname:window.__discoveryState==='active'?window.__snapshot.network.hostname:'',mdns_state:window.__discoveryState,mdns_reason:window.__discoveryState==='conflict'?'name_conflict':''})};
                if (window.__remoteFailure) return {ok:false,status:412,json:async()=>({error:{code:'revision_conflict'}})};
                return {ok:true,headers:{get:()=> '"remote-r1"'},json:async()=>({config:{version:1,enabled:false,station:{callsign:'AA0NT',locator:'EM18',power_dbm:20},wifi:{ssid:'Bench Wi-Fi',password:null,ntp_ipv4:'192.0.2.1'},schedules:[{period_s:120,phase_s:0}]}})};
            };
            window.WtpUi.populate({Transport:'network',Hostname:window.__snapshot.network.hostname,'TCP Port':18443,'Device ID':'a'.repeat(32),'TLS CA File':'/etc/wsprrypi/pico/ca.crt','TLS Client Certificate':'/etc/wsprrypi/pico/client.crt','TLS Client Key':'/etc/wsprrypi/pico/client.key','TLS Server Identity':'','Start Uncertainty ns':1000000,'Allow Frequency Adjustment':false,Endpoint:'/dev/preserved','USB Serial':'00001234','USB Vendor ID':51966,'USB Product ID':16402});
            window.WtpUi.developmentControlsVisible = true;
            window.WtpUi.select(true);
            document.getElementById('transmitter-hardware-tab').click();
        })()`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-network-state").textContent.includes("192.0.2.27")'), 'network snapshot');
        assert.equal(await evaluate('window.WtpUi.validate()'), true);
        await evaluate(`document.getElementById('wtp_transport').value='usb'; document.getElementById('wtp_transport').dispatchEvent(new Event('change',{bubbles:true}));`);
        assert.equal(await evaluate('window.WtpUi.read().Hostname'), 'wsprrypico-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.local');
        await evaluate(`document.getElementById('wtp_transport').value='network'; document.getElementById('wtp_transport').dispatchEvent(new Event('change',{bubbles:true})); document.getElementById('wtp-management').open=true; document.getElementById('wtp-remote-load').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-remote-callsign").value === "AA0NT"'), 'standalone settings');
        await evaluate(`document.getElementById('wtp-remote-callsign').value='MYDRAFT'; window.__remoteFailure=true; document.getElementById('wtp-remote-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-management-feedback").textContent.includes("changed elsewhere")'), 'revision feedback');
        assert.equal(await evaluate('document.getElementById("wtp-remote-callsign").value'), 'MYDRAFT');
        assert.equal(await evaluate('document.getElementById("wtp-remote-password").value'), '');
        await evaluate(`window.__remoteFailure=false; document.getElementById('wtp-remote-password').value='unsaved-secret'; document.getElementById('wtp-schedules-save').click();`);
        await waitFor(async () => await evaluate('document.getElementById("wtp-management-feedback").textContent.includes("schedules saved")'), 'schedule save');
        assert.equal(await evaluate('document.getElementById("wtp-remote-password").value'),'unsaved-secret');
        for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
            await client.send('Emulation.setDeviceMetricsOverride', {...viewport, deviceScaleFactor:1,mobile:viewport.name==='mobile'});
            for (const section of ['wtp-remote-password','wtp-management-feedback']) {
                await evaluate(`document.getElementById('${section}').scrollIntoView({block:'center',behavior:'instant'});`);
                await new Promise(resolve=>setTimeout(resolve,200));
                const shot=await client.send('Page.captureScreenshot',{format:'png'});
                fs.writeFileSync(path.join(output,`${viewport.name}-schedule-${section}.png`),Buffer.from(shot.data,'base64'));
            }
        }
        for (const discovery of ['active', 'conflict', 'failed']) {
            await evaluate(`window.__discoveryState='${discovery}'; document.getElementById('wtp-network-load').click();`);
            await waitFor(async () => await evaluate(`document.getElementById('wtp-management-feedback').textContent.includes('Discovery: ${discovery}.')`), 'discovery feedback');
            assert.equal(await evaluate('document.getElementById("wtp-remote-password").value'), 'unsaved-secret');
            for (const viewport of [{name:'desktop',width:1280,height:900},{name:'mobile',width:390,height:844}]) {
                await client.send('Emulation.setDeviceMetricsOverride', {...viewport,deviceScaleFactor:1,mobile:viewport.name==='mobile'});
                await evaluate(`document.getElementById('wtp-management-feedback').scrollIntoView({block:'center',behavior:'instant'});`);
                const shot = await client.send('Page.captureScreenshot',{format:'png'});
                fs.writeFileSync(path.join(output,`${viewport.name}-discovery-${discovery}.png`),Buffer.from(shot.data,'base64'));
                assert(await evaluate('document.documentElement.scrollWidth <= window.innerWidth + 1'), 'discovery horizontal overflow');
            }
        }
        await evaluate(`window.__snapshot.owns=true; window.__snapshot.job_id='b'.repeat(32); window.WtpUi.select(true);`);
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
            const full=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width:viewport.width,height:layout.cssContentSize.height,scale:1}});
            fs.writeFileSync(path.join(output,`${viewport.name}-full.png`),Buffer.from(full.data,'base64'));
            for (const section of ['wtp_transport','wtp_hostname','wtp-status-heading','wtp-management','wtp-management-feedback']) {
                await evaluate(`document.getElementById('${section}').scrollIntoView({block:'center',behavior:'instant'});`);
                await new Promise(resolve=>setTimeout(resolve,150));
                const shot=await client.send('Page.captureScreenshot',{format:'png'});
                fs.writeFileSync(path.join(output,`${viewport.name}-${section}.png`),Buffer.from(shot.data,'base64'));
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
        const failure=await client.send('Page.captureScreenshot',{format:'png'});
        fs.writeFileSync(path.join(output,'mobile-status-failure.png'),Buffer.from(failure.data,'base64'));
        console.log('Rendered desktop/mobile: transport selection, preserved inactive settings, management revision failure, status failure and drafts passed');
        console.log('Screenshots: '+output);
    } finally {
        if (client) client.close();
        if (chrome) chrome.kill('SIGTERM');
        php.kill('SIGTERM');
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
