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
    const fs=require("node:fs");
    const phpPort=await freePort(), debugPort=await freePort();
    const url=`http://127.0.0.1:${phpPort}/index.php?page=config`;
    const output=path.resolve(UI_ROOT,"../src/build/wtp-network/ui-usb-progress");fs.mkdirSync(output,{recursive:true});
    const profileDir=`/tmp/wsprrypi-usb-ui-${process.pid}`;
    let php,chrome,client;
    try {
        php=spawnFixtureChild("php",["-S",`127.0.0.1:${phpPort}`,"-t","data","tests/browser_fixture_router.php"],{cwd:UI_ROOT});
        await waitForSpawn(php);await waitFor(async()=>await getStatus(url)===200,"PHP fixture");
        chrome=spawnIsolatedProcess(process.env.CHROME_BIN || (process.platform==="darwin"?"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome":"chromium"),
            ["--headless","--no-sandbox","--disable-gpu","--no-first-run","--no-default-browser-check","--disable-background-networking",`--remote-debugging-port=${debugPort}`,`--user-data-dir=${profileDir}`,url]);
        await waitForSpawn(chrome);
        const page=await waitFor(async()=>(await getJson(`http://127.0.0.1:${debugPort}/json`)).find(p=>p.type==="page"),"Chrome",60000);
        client=new CdpClient(page.webSocketDebuggerUrl);await client.open();await client.send("Page.enable");
        const evaluate=async expression=>{const result=await client.send("Runtime.evaluate",{expression,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);return result.result.value;};
        await waitFor(async()=>await evaluate('document.readyState==="complete" && !!window.WtpUi && !!window.WtpUsbSettings'),"USB scripts");
        await evaluate(`(() => {
            configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();
            backendCurrentlyConnected=true;websocketCurrentlyConnected=true;syncConnectionAlert();clearBackendStatus('runtime');clearConfigLoadFailureState();
            const target={scope:'member',device_id:'a'.repeat(32),boot_id:'b'.repeat(32)};
            window.__snapshot={selected:true,transport:'usb',ready:true,management_available:true,host_utc_valid:true,phase:'idle',session_phase:'ready',now_ms:'10000',status_observed_ms:'9000',remote:{output_active:false},owns:false,
                identity:{device_id:target.device_id,boot_id:target.boot_id},common_name:'workbench-pico.local',common_name_confirmed:true,name_pending_restart:false,
                usb:{management:{connected:true,time:{accepted:true,selected:true,uncertainty_ns:'2400000',observed_ms:'9000'}},hostname:{saved:'workbench-pico.local'}}};
            window.fetch=async(url,options={})=>{
                const path=String(url),header={get:()=> '\\"usb-r1\\"'};
                let value=(path.includes('/api/wtp') || path.endsWith('/api/v1/status'))?window.__snapshot:
                    path.endsWith('/member/management')?{carrier:'usb_physical',connected:true,host:'',selection_id:'usb-selection',session:null}:
                    path.endsWith('/member/hostname')?{schema:'transmitter-hostname/1',target,saved:'workbench-pico.local',active:'workbench-pico.local',default:'wsprrypico-aaaaaaaa.local',writable:true,pending_restart:false}:
                    path.endsWith('/member/progress')?{schema:'transmitter-progress/1',target,draft_revision:'1',station:{callsign:'AA0NT',locator:null,power_dbm:null},station_complete:false,complete:false,apply_domains:{station:false},matches_applied:false,schedule_suspended:true}:
                    path.endsWith('/member/wifi')?{schema:'transmitter-wifi/1',target,wifi:null,pending_restart:false,storage_healthy:true}:
                    path.endsWith('/host/devices')?{scope:'wsprrypi-wtp-catalog/1',profiles:[],active_id:null}:
                    path.endsWith('/host/discovery')?{available:false,candidates:[],reason:'USB workflow'}:{};
                return {ok:true,status:200,headers:header,json:async()=>value};
            };
            WtpUi.populate({Transport:'usb',Endpoint:'/dev/serial/by-id/pico-if02','USB Serial':'PICO123','USB Vendor ID':51966,'USB Product ID':16402,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000,'Allow Frequency Adjustment':false});
            WtpUi.select(true);bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();
            document.getElementById('wtp-show-controls').checked=true;document.getElementById('wtp-show-controls').dispatchEvent(new Event('change'));
            document.getElementById('wtp-management').open=true;
        })()`);
        await waitFor(async()=>await evaluate("document.getElementById('wtp-status-heading').textContent==='workbench-pico.local'"),"common name");
        await evaluate("document.getElementById('wtp-usb-load').click()");
        await waitFor(async()=>await evaluate("document.getElementById('wtp-usb-feedback').textContent.includes('No saved Wi-Fi')"),"USB no-network state");
        await evaluate("document.getElementById('wtp-usb-progress-load').click()");
        await waitFor(async()=>await evaluate("document.getElementById('wtp-usb-progress-state').textContent.includes('station incomplete')"),"partial saved progress");
        const findings=[];
        for(const [name,width,height] of [["desktop",1440,1100],["mobile",390,844]]) {
            await client.send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:name==="mobile"});
            for(const theme of ["light","dark"]) {
                await evaluate(`applyThemePreference('${theme}');document.getElementById('wtp-usb-settings').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-110)`);
                await new Promise(resolve=>setTimeout(resolve,100));
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,`${name}/${theme} horizontal overflow`);
                assert.equal(await evaluate('document.getElementById("fleet-tab").textContent'),"Pico","normal toggle exposes Pico; advanced fleet schedules remain hidden");
                assert.equal(await evaluate('document.getElementById("wtp-usb-wifi-save").disabled'),false);
                const state=await evaluate(`(() => {const input=document.getElementById('wtp-usb-ssid'),s=getComputedStyle(input),r=input.getBoundingClientRect();input.focus();return {font:s.fontSize,height:r.height,width:r.width,focus:document.activeElement===input};})()`);
                assert(state.focus&&state.height>0&&state.width>150,JSON.stringify(state));findings.push({name,theme,...state});
                await evaluate("document.getElementById('wtp-usb-progress-heading').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-120)");
                const shot=await client.send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,`${name}-${theme}.png`),Buffer.from(shot.data,"base64"));
            }
        }
        fs.writeFileSync(path.join(output,"checks.json"),JSON.stringify(findings,null,2)+"\n");
        console.log("Rendered ordinary Pico toggle, configured common name and USB no-network editor at desktop/mobile in light/dark; no horizontal overflow; advanced fleet remains hidden; focus/input sizes passed");
        console.log(output);
    } finally {
        if(client)client.close();if(chrome)await stopIsolatedProcess(chrome);if(php)await stopChild(php);
        fs.rmSync(profileDir,{recursive:true,force:true});
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
