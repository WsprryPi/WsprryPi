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
            window.__mutations=[];
            window.__catalog={scope:'wsprrypi-wtp-catalog/1',profiles:[{id:'c'.repeat(32),name:'Saved precision Pico',method:'manual',settings:{Transport:'usb',Endpoint:'/dev/serial/by-id/pico-if02','USB Serial':'PICO123','USB Vendor ID':51966,'USB Product ID':16402,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000}}],active_id:'c'.repeat(32)};
            window.fetch=async(url,options={})=>{
                if(options.method && options.method!=='GET') {
                    window.__mutations.push({url:String(url),body:JSON.parse(options.body||'{}')});
                    if(String(url).endsWith('/host/devices') && options.method==='POST') {
                        const body=JSON.parse(options.body);
                        window.__catalog.profiles[0].settings=body.settings;
                    }
                }
                const path=String(url),header={get:()=> '\\"usb-r1\\"'};
                let value=(path.includes('/api/wtp') || path.endsWith('/api/v1/status'))?window.__snapshot:
                    path.endsWith('/member/management')?{carrier:'usb_physical',connected:true,host:'',selection_id:'usb-selection',session:null}:
                    path.endsWith('/member/hostname')?{schema:'transmitter-hostname/1',target,saved:'workbench-pico.local',active:'workbench-pico.local',default:'wsprrypico-aaaaaaaa.local',writable:true,pending_restart:false}:
                    path.endsWith('/member/progress')?{schema:'transmitter-progress/1',target,draft_revision:'1',station:{callsign:'AA0NT',locator:null,power_dbm:null},station_complete:false,complete:false,apply_domains:{station:false},matches_applied:false,schedule_suspended:true}:
                    path.endsWith('/member/wifi')?{schema:'transmitter-wifi/1',target,wifi:null,pending_restart:false,storage_healthy:true}:
                    path.endsWith('/host/devices')?window.__catalog:
                    path.endsWith('/host/discovery')?{available:false,candidates:[],reason:'USB workflow'}:{};
                return {ok:true,status:200,headers:header,json:async()=>value};
            };
            WtpUi.populate({Transport:'usb',Endpoint:'/dev/serial/by-id/pico-if02','USB Serial':'PICO123','USB Vendor ID':51966,'USB Product ID':16402,'Device ID':'a'.repeat(32),'Start Uncertainty ns':1000000,'Allow Frequency Adjustment':false});
            WtpUi.select(true);bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();

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
        // Exercise the actual profile editor and active form. Choosing the preset
        // has an explicit save; active host field edits use existing autosave.
        await waitFor(async()=>await evaluate("document.getElementById('fleet-edit').disabled===false"),"saved profile list");
        await evaluate("document.getElementById('fleet-add').click()");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"100000000");
        await evaluate("document.getElementById('fleet-editor-cancel').click();document.getElementById('fleet-edit').click()");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"1000000");
        for(const saved of [1000000,37000000,250000000]) {
            await evaluate(`WtpUi.populate({...WtpUi.read(),'Start Uncertainty ns':${saved}})`);
            assert.equal(await evaluate("WtpUi.read()['Start Uncertainty ns']"),saved);
        }
        await evaluate("document.getElementById('fleet-uncertainty-default').click();document.getElementById('wtp_uncertainty').value='100000000'");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"100000000");
        assert.equal(await evaluate("WtpUi.read()['Start Uncertainty ns']"),100000000);
        assert.equal(await evaluate("window.__mutations.length"),0,"preset actions must not save, connect or arm");
        assert.match(await evaluate("document.getElementById('fleet-editor-feedback').textContent"),/Save the profile/);
        assert.match(await evaluate("document.getElementById('wtp-uncertainty-hint').textContent"),/valid edits save automatically/);
        assert.equal(await evaluate("buildConfigPayload().WTP['Start Uncertainty ns']"),100000000);
        for(const [name,width,height] of [["desktop",1440,1100],["mobile",390,844]]) {
            await client.send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:name==="mobile"});
            for(const theme of ["light","dark"]) {
                await evaluate(`applyThemePreference('${theme}')`);
                for(const surface of ["fleet","wtp"]) {
                    const field=surface==="fleet"?"fleet-uncertainty":"wtp_uncertainty";
                    const preset=surface==="fleet"?"fleet-uncertainty-default":field;
                    await evaluate(`document.getElementById('${field}').closest('.config-stacked-field').scrollIntoView({block:'center',behavior:'instant'});document.getElementById('${preset}').focus()`);
                    await new Promise(resolve=>setTimeout(resolve,100));
                    const state=await evaluate(`(() => {const input=document.getElementById('${field}'),button=document.getElementById('${preset}'),r=input.getBoundingClientRect();return {focus:document.activeElement===button,width:r.width,height:r.height,value:input.value,overflow:document.documentElement.scrollWidth>innerWidth};})()`);
                    assert(state.focus&&state.width>150&&state.height>0&&!state.overflow,JSON.stringify(state));
                    findings.push({name,theme,surface,...state});
                    const shot=await client.send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,`policy-${surface}-${name}-${theme}.png`),Buffer.from(shot.data,"base64"));
                }
            }
        }
        await evaluate("window.confirm=()=>true;document.getElementById('fleet-save').click()");
        await waitFor(async()=>await evaluate("document.getElementById('fleet-editor').hidden"),"explicit profile save");
        const mutations=await evaluate("window.__mutations");
        assert.equal(mutations.length,1);
        assert(mutations[0].url.endsWith('/host/devices'));
        assert.equal(mutations[0].body.settings['Start Uncertainty ns'],100000000);
        assert.equal(mutations[0].body.operation,'edit');
        assert.equal(await evaluate("window.__catalog.profiles[0].settings['USB Serial']"),'PICO123');
        await evaluate("document.getElementById('fleet-edit').click()");
        assert.equal(await evaluate("document.getElementById('fleet-uncertainty').value"),"100000000");
        fs.writeFileSync(path.join(output,"checks.json"),JSON.stringify(findings,null,2)+"\n");
        console.log("Rendered ordinary Pico toggle, configured common name and USB no-network editor at desktop/mobile in light/dark; no horizontal overflow; advanced fleet remains hidden by default; 100 ms profile draft-only adoption, active form collection, explicit profile save/reload and focus/input sizes passed");
        console.log(output);
    } finally {
        if(client)client.close();if(chrome)await stopIsolatedProcess(chrome);if(php)await stopChild(php);
        fs.rmSync(profileDir,{recursive:true,force:true});
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
