// SPDX-License-Identifier: MIT
// Shipped native browser -> protected Pi coordinator -> actual USB/Pico fixture.
"use strict";

const assert = require("node:assert/strict");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");
const os = require("node:os");
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

    send(method, params = {}, timeoutMs = 15000) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const timer=setTimeout(()=>{this.pending.delete(id);reject(new Error("CDP deadline: "+method));},timeoutMs);
            this.pending.set(id, { resolve:value=>{clearTimeout(timer);resolve(value);}, reject:error=>{clearTimeout(timer);reject(error);} });
            this.socket.send(JSON.stringify({ id, method, params }));
        });
    }

    close() {
        this.socket.close();
    }
}

async function main() {
 const fs=require('node:fs'),{spawn}=require('node:child_process'),readline=require('node:readline');
 const controlRequest=require('./control_pair_request.js');
 const [binary,driver]=process.argv.slice(2);assert(binary&&driver,'provide Pi pair client and Pico driver');
 const output=path.resolve(process.env.WSPRRYPI_SI_UI_EVIDENCE_DIR||'src/build/si5351-native-ui');fs.mkdirSync(output,{recursive:true});
 const profile=fs.mkdtempSync(path.join(os.tmpdir(),'si5351-pi-native-'));
 const phpPort=await freePort(),debugPort=await freePort();let php,chrome,client,pair;
 try {
  pair=spawn(path.resolve(binary),[path.resolve(driver)],{stdio:['pipe','pipe','inherit']});
  const queue=[],waiting=[];const lines=readline.createInterface({input:pair.stdout});lines.on('line',s=>waiting.length?waiting.shift()(s):queue.push(s));
  const next=()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('pair deadline')),20000);waiting.push(s=>{clearTimeout(t);resolve(s)});});
  let work=Promise.resolve();
  const command=r=>{const result=work.then(async()=>{pair.stdin.write(JSON.stringify(controlRequest(r))+'\n');return JSON.parse(await next());});work=result.catch(()=>{});return result;};
  const initial=await command({method:'GET',path:'/api/control/v1/member/progress'});assert.equal(initial.status,200,JSON.stringify(initial));
  const updated=await command({method:'POST',path:'/api/control/v1/member/progress/update',revision:initial.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'progress',target:initial.body.target,request_id:'1'.repeat(32),fields:{callsign:'K1ABC',locator:'EM18',power_dbm:20}})});assert.equal(updated.status,200,JSON.stringify(updated));
  const current=await command({method:'GET',path:'/api/control/v1/member/progress'});
  const seeded=await command({method:'POST',path:'/api/control/v1/member/progress/apply',revision:current.etag,body:JSON.stringify({contract:'Wsprry-Control/1',schema:'progress',target:current.body.target,application:{draft_revision:current.body.draft_revision,domain:'station'}})});assert.equal(seeded.status,200,JSON.stringify(seeded));
  php=spawnFixtureChild(process.env.PHP_BIN||'php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:UI_ROOT});await waitForSpawn(php);
  const url=`http://127.0.0.1:${phpPort}/index.php?page=config`;await waitFor(async()=>await getStatus(url)===200,'PHP');
  chrome=spawnIsolatedProcess(process.env.CHROME_BIN||(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),['--headless','--no-sandbox','--disable-gpu','--disable-background-networking',`--remote-debugging-port=${debugPort}`,`--user-data-dir=${profile}`,'about:blank']);await waitForSpawn(chrome);
  const page=await waitFor(async()=>(await getJson(`http://127.0.0.1:${debugPort}/json`)).find(p=>p.type==='page'),'Chrome',60000);client=new CdpClient(page.webSocketDebuggerUrl);await client.open();await client.send('Page.enable');await client.send('Page.navigate',{url},60000);
  const evaluate=async expression=>{const r=await client.send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.exception?.description||r.exceptionDetails.text);return r.result.value;};
  await waitFor(()=>evaluate('document.readyState==="complete" && !!window.WtpManagement && !!window.WsprrySi5351'),'shipped controls');
  await client.send('Runtime.addBinding',{name:'siPair'});
  client.socket.on('message',async raw=>{const m=JSON.parse(raw);if(m.method!=='Runtime.bindingCalled'||m.params.name!=='siPair')return;const {id,request}=JSON.parse(m.params.payload);try{const result=await command(request);await evaluate(`siReplies.get(${id})(${JSON.stringify(result)});siReplies.delete(${id})`);}catch(e){console.error(e);}});
  await evaluate(`(()=>{configAutosaveSuspended=true;clearPendingPopulateConfigRetry();clearWebSocketReconnectTimer();backendCurrentlyConnected=true;websocketCurrentlyConnected=true;syncConnectionAlert();clearBackendStatus('runtime');clearConfigLoadFailureState();globalThis.siSnapshot={selected:true,transport:'usb',ready:true,management_available:true,host_utc_valid:true,phase:'idle',session_phase:'ready',now_ms:'10000',status_observed_ms:'9000',remote:{output_active:false},owns:false,identity:{device_id:'a'.repeat(32),boot_id:'b'.repeat(32)},common_name:'workbench-pico.local',common_name_confirmed:true,name_pending_restart:false,usb:{management:{connected:true,time:{accepted:true,selected:true,uncertainty_ns:'2400000',observed_ms:'9000'}},hostname:{saved:'workbench-pico.local'}}};globalThis.siReplies=new Map();globalThis.siSerial=0;globalThis.siWrites=[];window.fetch=async(url,o={})=>{const path=String(url);if(!path.startsWith('/api/control/v1/member/'))return {ok:true,status:200,headers:{get:()=>''},json:async()=>path.includes('/api/wtp') || path.endsWith('/api/control/v1/status')?siSnapshot:path.endsWith('/host/devices')?{scope:'wsprrypi-wtp-catalog/1',profiles:[],active_id:null}:{}};if(o.method && o.method!=='GET'&&!path.endsWith('/validate'))siWrites.push({path,body:o.body});const r=await new Promise(resolve=>{const id=++siSerial;siReplies.set(id,resolve);siPair(JSON.stringify({id,request:{path,method:o.method||'GET',body:o.body||'',revision:o.headers?.['If-Match']||'',member_context:o.headers?.['X-WsprryPi-Member-Context']||''}}))});return {ok:r.status===200,status:r.status,headers:{get:n=>n==='ETag'?r.etag:null},json:async()=>r.body};};WtpUi.populate({Transport:'usb',Endpoint:'/dev/pico','USB Serial':'PICO123','Device ID':'a'.repeat(32)});WtpUi.select(true);WtpManagement.setAvailability(true,true);document.getElementById('wtp-management').open=true;bootstrap.Tab.getOrCreateInstance(document.getElementById('transmitter-hardware-tab')).show();bootstrap.Tab.getOrCreateInstance(document.getElementById('fleet-tab')).show();})()`);
  await waitFor(()=>evaluate(`!document.getElementById('wtp-member-load').disabled`),'member controls');
  await evaluate(`document.getElementById('wtp-member-load').click()`);
  await waitFor(()=>evaluate(`document.getElementById('wtp-member-feedback').textContent.includes('loaded')`),'resource load').catch(async error=>{throw Error(error.message+' '+JSON.stringify(await evaluate(`({feedback:document.getElementById('wtp-management-feedback').textContent,resources:document.getElementById('wtp-member-feedback').textContent})`)))});
  await evaluate(`globalThis.siBefore=siWrites.length;const e=document.getElementById('wtp-member-engine');e.value='i2c';e.dispatchEvent(new Event('change'));`);
  assert.equal(await evaluate(`document.getElementById('wtp-si5351-controls').hidden`),false);
  await evaluate(`document.getElementById('wtp-member-validate').click()`);
  assert.equal(await evaluate('siWrites.length'),await evaluate('siBefore'));
  assert.equal(await evaluate('document.activeElement.dataset.si'),'pair');
  await evaluate(`globalThis.siSet=(k,v)=>{const e=document.querySelector('#wtp-si5351-controls [data-si="'+k+'"]');e.value=v;e.dispatchEvent(new Event('change',{bubbles:true}))};siSet('pair','1');siSet('reference_hz','25000000');siSet('reference_source','external_xa');siSet('transmit_output','0');siSet('loopback_output','2');siSet('loopback_input_gp','7');document.getElementById('wtp-member-validate').click()`);
  await waitFor(()=>evaluate(`!document.getElementById('wtp-member-hardware-save').disabled`),'valid complete allocation');
  for(const [name,width,height] of [['desktop',1440,1100],['mobile',390,844]])for(const theme of ['light','dark']) {
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:name==='mobile'});
   await evaluate(`applyThemePreference('${theme}')`);await new Promise(resolve=>setTimeout(resolve,150));
   await evaluate(`document.getElementById('wtp-management').open=true;document.getElementById('wtp-si5351-controls').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-130)`);
   await new Promise(resolve=>setTimeout(resolve,100));
   assert(await evaluate('document.documentElement.scrollWidth<=innerWidth'));
   const rect=await evaluate(`(()=>{const r=document.getElementById('wtp-si5351-controls').getBoundingClientRect();return {top:r.top,bottom:r.bottom,height:r.height,viewport:innerHeight}})()`);
   assert(rect.height>0 && rect.top<rect.viewport && rect.bottom>0,'editor in viewport: '+JSON.stringify(rect));
   const shot=await client.send('Page.captureScreenshot',{format:'png'});fs.writeFileSync(path.join(output,`${name}-${theme}.png`),Buffer.from(shot.data,'base64'));
  }
  await evaluate(`document.getElementById('wtp-member-hardware-save').click()`);
  await waitFor(()=>evaluate(`document.getElementById('wtp-member-hardware-feedback').textContent.includes('RF output settings saved')`),'atomic saved/readback').catch(async error=>{throw Error(error.message+' '+JSON.stringify(await evaluate(`({feedback:document.getElementById('wtp-management-feedback').textContent,hardware:document.getElementById('wtp-member-hardware-feedback').textContent,resources:document.getElementById('wtp-member-feedback').textContent,writes:siWrites,disabled:document.getElementById('wtp-member-hardware-save').disabled})`)))});
  assert.equal(await evaluate(`JSON.parse(siWrites.at(-1).body).config.si5351.loopback_output`),2);
  assert.equal(await evaluate(`siWrites.at(-1).path`),'/api/control/v1/member/configuration');
  assert.equal(await evaluate(`document.getElementById('wtp-member-engine').value`),'i2c');
  console.log('Native Pi Si5351: shipped controls, validation focus, no draft mutation, Bootstrap desktop/mobile light/dark, atomic protected USB/Pico save and refreshed resources passed');
 } finally {if(client)client.close();await stopIsolatedProcess(chrome);await stopChild(php);if(pair){pair.stdin.end();pair.kill();}}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
