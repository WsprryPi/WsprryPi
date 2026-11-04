"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");
const { spawnFixtureChild, waitForSpawn, spawnIsolatedProcess, stopIsolatedProcess, stopChild } = require("./browser_fixture_process.js");
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
                try { resolve(JSON.parse(body)); } catch (error) { reject(error); }
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
    close() { this.socket.close(); }
}


async function main() {
 const phpPort=await freePort(), debugPort=await freePort();
 let php, chromium, client;
 const evidence=process.env.WSPRRYPI_PPS_EVIDENCE_DIR;
 if(evidence) fs.mkdirSync(evidence,{recursive:true});
 const evaluate=async expression=>{
  const result=await client.send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
  if(result.exceptionDetails) throw Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);
  return result.result.value;
 };
 try {
  php=spawnFixtureChild('php',['-S',`127.0.0.1:${phpPort}`,'-t','data','tests/browser_fixture_router.php'],{cwd:UI_ROOT});
  await waitForSpawn(php);
  await waitFor(async()=>await getStatus(`http://127.0.0.1:${phpPort}/index.php?page=config`)===200,'PHP fixture');
  chromium=spawnIsolatedProcess(process.env.CHROME_BIN || (process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':'chromium'),
   ['--headless','--no-sandbox','--disable-gpu',`--remote-debugging-port=${debugPort}`,`--user-data-dir=/tmp/wsprrypi-pps-browser-${process.pid}`,'about:blank']);
  await waitForSpawn(chromium);
  const page=await waitFor(async()=>(await getJson(`http://127.0.0.1:${debugPort}/json`)).find(p=>p.type==='page'),'Chromium');
  client=new CdpClient(page.webSocketDebuggerUrl); await client.open();
  await client.send('Page.navigate',{url:`http://127.0.0.1:${phpPort}/index.php?page=config`});
  await waitFor(()=>evaluate('document.readyState==="complete" && typeof buildConfigPayload==="function"'),'shipped setup');
  await evaluate(`
   globalThis.autosaveCalls=0;scheduleAutosave=()=>{autosaveCalls++};
   window.WSPRRYPI_PLATFORM={...(window.WSPRRYPI_PLATFORM||{}),raspberryPiGeneration:5,rp1GpioOperatorVisible:true,gpioClockTransmissionSupported:true};
   setTransmitBackendSelection('gpio');clickTransmitBackend();
   document.querySelectorAll('.modal.show').forEach(el=>{bootstrap.Modal.getInstance(el)?.hide();el.classList.remove('show');el.style.display='none'});
   document.querySelectorAll('.modal-backdrop').forEach(el=>el.remove());document.body.classList.remove('modal-open');
   document.getElementById('transmitter-hardware-tab').click();
   document.getElementById('use_system_clock_frequency_estimate').checked=false;
   document.getElementById('pps_input_pin').value=18;
   document.getElementById('pps_input_pin').dispatchEvent(new Event('change',{bubbles:true}));
   document.getElementById('use_direct_pps_calibration').checked=true;
   document.getElementById('use_direct_pps_calibration').dispatchEvent(new Event('change',{bubbles:true}));
  `);
  assert.equal(await evaluate(`(() => {
   const node=document.getElementById('direct-pps-status');
   const value=normalizeRuntimeStatus({frequency_estimate_provider:'RP1 PPS',frequency_estimate_qualification:'converging',frequency_estimate_reason:'Waiting for stable PPS.'});
   renderGpioFrequencyCorrection(value);const original=node.firstChild;
   renderGpioFrequencyCorrection(value);return node.firstChild===original;
  })()`),true,'unchanged polls do not recreate the live-status text and repeat announcements');
  assert.equal(await evaluate('document.getElementById("gpio_frequency_residual_ppm").disabled'),false,'direct PPS enables residual with host estimate off');
  assert.equal(await evaluate('buildConfigPayload().GPIO["Use Direct PPS Calibration"]'),true,'producer persists explicit opt-in');
  assert.equal(await evaluate('buildConfigPayload().GPIO["Use System Clock Frequency Estimate"]'),false,'host preference stays independent');
  assert.equal(await evaluate('buildConfigPayload().GPIO["PPS Input Pin"]'),18,'producer persists selected BCM GPIO');
  assert(await evaluate('autosaveCalls>0'),'new switch participates in autosave');
  await evaluate("document.getElementById('use_direct_pps_calibration').focus()");
  for(let pass=0;pass<2;pass++) {
   await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:' ',code:'Space',windowsVirtualKeyCode:32});
   await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:' ',code:'Space',windowsVirtualKeyCode:32});
   assert.equal(await evaluate('document.getElementById("use_direct_pps_calibration").checked'),pass===1,'keyboard switch preserves native behavior');
  }
  await evaluate("setLEDPin(18);document.getElementById('use_led').checked=true;validateTransmitterHardwareFields()");
  assert.equal(await evaluate('document.getElementById("direct-pps-error").hidden'),false,'PPS pin conflict feedback stays beside its control');
  assert.equal(await evaluate('document.getElementById("use_direct_pps_calibration").checkValidity()'),false,'conflict blocks saving without discarding the opt-in draft');
  await evaluate("document.getElementById('use_led').checked=false;validateTransmitterHardwareFields()");
  assert.equal(await evaluate('document.getElementById("direct-pps-error").hidden'),true,'repair clears the local conflict');
  await evaluate(`globalThis.draft=buildConfigPayload();sessionStorage.setItem(CONFIG_DRAFT_STORAGE_KEY,JSON.stringify({payload:draft}));document.getElementById('use_direct_pps_calibration').checked=false;suppressNextConfigDraftRestore=false;lastSavedConfigPayload='{}';restorePersistedConfigDraft();`);
  assert.equal(await evaluate('document.getElementById("use_direct_pps_calibration").checked'),true,'draft restores direct PPS');
  assert.equal(await evaluate('document.getElementById("use_system_clock_frequency_estimate").checked'),false,'draft preserves independent host preference');
  assert.equal(await evaluate('document.getElementById("pps_input_pin").value'),'18','draft restores configured PPS GPIO');
  await evaluate("document.getElementById('pps_input_pin').value=4;validateTransmitterHardwareFields()");
  assert.equal(await evaluate('document.getElementById("pps_input_pin").checkValidity()'),false,'PPS cannot share RF output');
  await evaluate("document.getElementById('pps_input_pin').value=18.5;validateTransmitterHardwareFields()");
  assert.equal(await evaluate('document.getElementById("pps_input_pin").checkValidity()'),false,'PPS requires a whole GPIO number');
  await evaluate("document.getElementById('pps_input_pin').value=18;validateTransmitterHardwareFields()");
  for (const generation of [5,4]) {
   await evaluate(`window.WSPRRYPI_PLATFORM.raspberryPiGeneration=${generation};updateBackendPlatformSupportUi();refreshTransmitGpioOptions();syncCalibrationControls()`);
  for(const [device,width,height] of [['desktop',1440,1000],['mobile',390,844]]) {
   await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:device==='mobile'});
   for(const theme of ['light','dark']) for(const [state,reason] of [
    ['converging','Collecting three stable, distinct ten-second PPS measurements.'],
    ['qualified','RF clock counted over ten PPS periods; conditional uncertainty <=0.202 ppm with a 1 us reference-edge budget.'],
    ['unavailable','PPS capture timed out; reference missing or interrupted. Using configured manual fallback.']]) {
    await evaluate(`document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});renderGpioFrequencyCorrection(normalizeRuntimeStatus({transmit_backend:${JSON.stringify(generation===5?'rp1-gpclk':'gpio')},frequency_estimate_provider:${JSON.stringify(generation===5?'RP1 PPS':'BCM PPS')},frequency_estimate_qualification:${JSON.stringify(state)},frequency_estimate_reason:${JSON.stringify(generation===5?reason:reason.replace('ten-second','120-second').replace('ten PPS','120 PPS').replace('0.202','0.250'))},frequency_estimate_age_seconds:2}));document.getElementById('gpio-calibration-heading').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-90)`);
    assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,`${device} page overflow`);
    assert.match(await evaluate('document.getElementById("direct-pps-status").textContent'),new RegExp(state));
    if(evidence) {
     const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
     fs.writeFileSync(path.join(evidence,`${generation===5?"rp1":"bcm"}-${device}-${theme}-${state}.png`),shot.data,'base64');
     await evaluate("document.getElementById('direct-pps-status').scrollIntoView({block:'end',behavior:'instant'});window.scrollBy(0,70)");
     const detail=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
     fs.writeFileSync(path.join(evidence,`${generation===5?"rp1":"bcm"}-${device}-${theme}-${state}-detail.png`),detail.data,'base64');
    }
   }
  }
  }
  await evaluate(`document.getElementById('use_direct_pps_calibration').checked=false;document.getElementById('use_direct_pps_calibration').dispatchEvent(new Event('change',{bubbles:true}));`);
  assert.equal(await evaluate('document.getElementById("gpio_frequency_residual_ppm").disabled'),true,'manual-only source does not apply residual');
  await evaluate("window.WSPRRYPI_PLATFORM.raspberryPiGeneration=4;syncCalibrationControls()");
  assert.equal(await evaluate('document.getElementById("use_direct_pps_calibration").disabled'),false,'Pi4 supports BCM direct PPS');
  assert.match(await evaluate('document.getElementById("direct-pps-settling").textContent'),/six minutes/);
  await evaluate(`document.getElementById('use_direct_pps_calibration').checked=true;document.getElementById('use_direct_pps_calibration').dispatchEvent(new Event('change',{bubbles:true}));renderGpioFrequencyCorrection(normalizeRuntimeStatus({transmit_backend:'gpio',frequency_estimate_provider:'BCM PPS',frequency_estimate_qualification:'converging',frequency_estimate_reason:'Collecting three stable, distinct 120-second PPS measurements.'}));`);
  assert.equal(await evaluate('document.getElementById("gpio_frequency_residual_ppm").disabled'),false,'BCM direct source enables residual');
  assert.match(await evaluate('document.getElementById("direct-pps-status").textContent'),/Direct PPS: converging/);
  for (const generation of [1,2,3]) {
    await evaluate(`window.WSPRRYPI_PLATFORM.raspberryPiGeneration=${generation};updateBackendPlatformSupportUi();refreshTransmitGpioOptions();syncCalibrationControls()`);
    assert.equal(await evaluate('document.getElementById("use_direct_pps_calibration").disabled'),false,'Pi1–3 support BCM direct PPS');
  }
  await evaluate("window.WSPRRYPI_PLATFORM.raspberryPiGeneration=0;syncCalibrationControls()");
  assert.equal(await evaluate('document.getElementById("use_direct_pps_calibration").disabled'),true,'unsupported hosts cannot opt in');
  console.log('PPS browser: explicit autosave, independent host preference, retained draft, residual availability, live acquisition/qualified/fallback states, four size/theme combinations passed');
 } finally {
  if(client) client.close();
  if(chromium) await stopIsolatedProcess(chromium);
  if(php) await stopChild(php);
 }
}
main().catch(error=>{console.error(error);process.exitCode=1});
