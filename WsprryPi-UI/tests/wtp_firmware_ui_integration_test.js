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
    const fs=require("node:fs"),os=require("node:os");
    const phpPort=await freePort(),debugPort=await freePort();
    const url=`http://127.0.0.1:${phpPort}/index.php?page=config`;
    const output=path.resolve(UI_ROOT,"../src/build/wtp-network/ui-firmware");fs.mkdirSync(output,{recursive:true});
    const profileDir=fs.mkdtempSync(path.join(os.tmpdir(),"wsprrypi-firmware-ui-"));
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
        await waitFor(async()=>await evaluate('document.readyState==="complete" && !!window.WtpUi && !!window.WtpFirmware'),"firmware scripts");
        await evaluate(`(() => {
            configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();
            backendCurrentlyConnected=true;websocketCurrentlyConnected=true;syncConnectionAlert();clearBackendStatus('runtime');clearConfigLoadFailureState();
            window.__snapshot={selected:true,transport:'usb',ready:true,management_available:true,host_utc_valid:true,phase:'idle',session_phase:'ready',now_ms:'10000',status_observed_ms:'9000',remote:{output_active:false},owns:false,identity:{device_id:'c'.repeat(32),boot_id:'b'.repeat(32)}};
            window.__base={serial:'1545B6EFBF78028C',device_id:'c'.repeat(32),installed_version:'0.1.0-ci.39084514130.1',selection_id:'fixture-selection',running:false,maintenance:false};
            window.__fw={...__base};window.__requests=[];
            window.fetch=async(url,options={})=>{
                if(String(url).endsWith('/member/firmware')) {
                    if(options.method==='POST')window.__requests.push(JSON.parse(options.body));
                    return {ok:true,status:200,headers:{get:()=>''},json:async()=>window.__fw};
                }
                const value=String(url).endsWith('/api/v1/status')||String(url).includes('/api/wtp')?window.__snapshot:
                    String(url).endsWith('/host/devices')?{scope:'wsprrypi-wtp-catalog/1',profiles:[],active_id:null}:
                    String(url).endsWith('/host/discovery')?{available:false,candidates:[],reason:'fixture'}:{};
                return {ok:true,status:200,headers:{get:()=>''},json:async()=>value};
            };
            WtpUi.populate({Transport:'usb',Endpoint:'/dev/serial/by-id/pico-if02','USB Serial':'1545B6EFBF78028C','USB Vendor ID':51966,'USB Product ID':16402,'Device ID':'c'.repeat(32)});
            WtpUi.select(true);bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();

            WtpFirmware.setAvailability(true);document.getElementById('wtp-firmware').open=true;
            window.confirm=()=>true;
        })()`);
        assert.equal(await evaluate("document.getElementById('wtp-firmware-branch').value"),"main");
        const states={
            checked:{operation:'plan',plan_id:'fixture-plan',result:{ok:true,branch:'main',version:'0.1.0-ci.40000000000.1',source:'a'.repeat(40),workflow:{run_id:40000000000,attempt:1}}},
            pending:{operation:'upgrade',running:true,maintenance:true,result:null},
            refusal:{operation:'plan',result:{ok:false,error:'No verified main CI download is available.'}},
            failure:{operation:'upgrade',maintenance:true,recovery_plan_id:'fixture-plan',recovery_version:'0.1.0-ci.40000000000.1',result:{ok:false,error:'Saved semantic state differs after reboot; keep member in maintenance'}},
            reconnect:{operation:'recover',maintenance:true,recovery_plan_id:'fixture-plan',recovery_version:'0.1.0-ci.40000000000.1',result:{ok:false,error:'Firmware verified; Pi reconnection remains unconfirmed.'}},
            success:{operation:'upgrade',result:{ok:true,version:'0.1.0-ci.40000000000.1'}}
        };
        const findings=[];
        for(const [name,width,height] of [["desktop",1440,1100],["mobile",390,844]]) {
            await client.send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:name==="mobile"});
            for(const theme of ["light","dark"]) for(const [state,value] of Object.entries(states)) {
                await evaluate(`applyThemePreference('${theme}');window.__fw={...window.__base,...${JSON.stringify(value)}};document.getElementById('wtp-firmware-read').click()`);
                await waitFor(async()=>await evaluate("document.getElementById('wtp-firmware-read').disabled===false"),"firmware status read");
                await evaluate("document.getElementById('wtp-firmware').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-110)");
                await new Promise(resolve=>setTimeout(resolve,100));
                const observed=await evaluate(`(() => {const b=document.getElementById('wtp-firmware-upgrade'),r=b.getBoundingClientRect();return {overflow:document.documentElement.scrollWidth>innerWidth,installDisabled:b.disabled,recoverHidden:document.getElementById('wtp-firmware-recover').hidden,buttonHeight:r.height,text:document.getElementById('wtp-firmware-feedback').textContent};})()`);
                assert(!observed.overflow,`${name}/${theme}/${state} horizontal overflow`);assert(observed.buttonHeight>=36);
                assert.equal(observed.installDisabled,state!=="checked");assert.equal(observed.recoverHidden,!value.maintenance);
                findings.push({name,theme,state,...observed});
                const shot=await client.send("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(output,`${name}-${theme}-${state}.png`),Buffer.from(shot.data,"base64"));
            }
        }
        fs.writeFileSync(path.join(output,"checks.json"),JSON.stringify(findings,null,2)+"\n");
        console.log("Actual firmware UI rendered at desktop/mobile light/dark: checked, pending, refusal, failure, reconnect and success; no overflow; buttons and recovery gates passed");console.log(output);
    } finally {
        if(client)client.close();if(chrome)await stopIsolatedProcess(chrome);if(php)await stopChild(php);
        fs.rmSync(profileDir,{recursive:true,force:true});
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
