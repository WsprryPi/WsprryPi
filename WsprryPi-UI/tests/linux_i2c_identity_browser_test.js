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
    const phpPort = await freePort(), debugPort = await freePort();
    let php, chromium, client;
    const evidence = process.env.WSPRRYPI_I2C_EVIDENCE_DIR;
    if (evidence) fs.mkdirSync(evidence, {recursive:true});
    async function evaluate(expression) {
        const result = await client.send("Runtime.evaluate", {expression, returnByValue:true, awaitPromise:true});
        if (result.exceptionDetails) throw Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
        return result.result.value;
    }
    try {
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${phpPort}`, "-t", "data", "tests/browser_fixture_router.php"], {cwd:UI_ROOT});
        await waitForSpawn(php);
        await waitFor(async()=>await getStatus(`http://127.0.0.1:${phpPort}/index.php?page=config`)===200,"PHP fixture");
        chromium=spawnIsolatedProcess(process.env.CHROME_BIN || (process.platform==='darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'chromium'),
            ['--headless','--no-sandbox','--disable-gpu',`--remote-debugging-port=${debugPort}`,`--user-data-dir=/tmp/wsprrypi-i2c-browser-${process.pid}`,'about:blank']);
        await waitForSpawn(chromium);
        const page=await waitFor(async()=>(await getJson(`http://127.0.0.1:${debugPort}/json`)).find(p=>p.type==='page'),'Chromium');
        client=new CdpClient(page.webSocketDebuggerUrl);await client.open();
        await client.send('Page.navigate',{url:`http://127.0.0.1:${phpPort}/index.php?page=config`});
        await waitFor(()=>evaluate('document.readyState==="complete" && typeof populateI2cBuses==="function"'),'configuration UI');
        await evaluate(`
            // All replies are fixtures; these workflows never contact a transmitter.
            globalThis.i2cRequests=[];
            scheduleAutosave=()=>{};
            ajaxWithEndpointFallback=(endpoint,options)=>{const r={endpoint,options};i2cRequests.push(r);return {done(fn){r.done=fn;return this},fail(fn){r.fail=fn;return this}}};
            document.getElementById('transmit_backend').checked=true;
            window.WSPRRYPI_PLATFORM={...(window.WSPRRYPI_PLATFORM||{}),i2cBuses:[],i2cBusDiscoveryError:'',si5351Detected:false};
            clickTransmitBackend();
            document.querySelectorAll('.modal.show').forEach(el=>{bootstrap.Modal.getInstance(el)?.hide();el.classList.remove('show');el.style.display='none'});
            document.querySelectorAll('.modal-backdrop').forEach(el=>el.remove());document.body.classList.remove('modal-open');
            document.getElementById('si5351_reference_frequency').value='25000000';document.getElementById('si5351_reference_source').value='crystal';document.getElementById('si5351_reference_source').dispatchEvent(new Event('change',{bubbles:true}));document.getElementById('ppm').value='0';
            const tab=document.getElementById('transmitter-hardware-tab');tab.click();
            globalThis.testIdentity='usb:04d8:00dd:serial:6164616672756974';
            globalThis.testBus=(n,id=testIdentity)=>({Number:n,Name:'MCP2221A',Identity:id});
            globalThis.loadAdapters=(buses,id=testIdentity)=>{window.WSPRRYPI_PLATFORM.i2cBuses=buses;populateI2cBuses(1,id);};
            loadAdapters([]);
        `);
        assert.equal(await evaluate('Number.isNaN(selectedI2cBusValue())'),true);
        assert.match(await evaluate('document.getElementById("si5351-bus-hint").textContent'),/No I2C adapters/);
        await evaluate('loadAdapters([testBus(15)])');
        assert.equal(await evaluate('selectedI2cBusValue()'),15);
        assert.equal(await evaluate('selectedAdapterIdentity()'), 'usb:04d8:00dd:serial:6164616672756974');
        await evaluate('loadAdapters([testBus(22)])');
        assert.equal(await evaluate('selectedI2cBusValue()'),22);
        assert.equal(await evaluate('document.getElementById("si5351_i2c_bus").numericBus'),1,'bus renumbering never rewrites persisted numeric selection');
        await evaluate('loadAdapters([testBus(22),testBus(23)])');
        assert.equal(await evaluate('Number.isNaN(selectedI2cBusValue())'),true);
        assert.match(await evaluate('document.getElementById("si5351-bus-hint").textContent'),/ambiguous/);
        assert.equal(await evaluate('document.getElementById("si5351_i2c_bus").classList.contains("is-invalid")'),true);
        assert.equal(await evaluate('si5351AddressInventoryState.loading'),false);
        await evaluate('loadAdapters([testBus(22)]);refreshSi5351Addresses(selectedI2cBusValue(),"0x60")');
        assert.equal(await evaluate('i2cRequests.at(-1).options.data.identity'), 'usb:04d8:00dd:serial:6164616672756974');
        assert.equal(await evaluate('i2cRequests.at(-1).options.data.address'),96);
        await evaluate('i2cRequests.at(-1).done({"I2C Bus":22,Addresses:[],"Discovery Error":"Access denied; check I2C permissions."})');
        assert.match(await evaluate('document.getElementById("si5351-address-hint").textContent'),/Access denied/);
        // An absent/old configured address must not prevent explicit recovery.
        await evaluate('populateSi5351Addresses(22,"0x61",[],"",22);syncBackendControlAvailability()');
        assert.equal(await evaluate('document.getElementById("si5351_i2c_address").disabled'),false);
        assert.match(await evaluate('document.getElementById("si5351-address-hint").textContent'),/No compatible response at 0x61/);
        await evaluate('document.getElementById("si5351_i2c_address").value="0x60";document.getElementById("si5351_i2c_address").dispatchEvent(new Event("change",{bubbles:true}))');
        assert.equal(await evaluate('i2cRequests.at(-1).options.data.address'),96,'explicit recovery probes only the newly selected address');
        await evaluate('i2cRequests.at(-1).done({"I2C Bus":22,Addresses:["0x60"],"Discovery Error":""})');
        assert.equal(await evaluate('document.getElementById("si5351_i2c_address").value'),'0x60');
        await evaluate(`
            const draft=buildConfigPayload();
            draft.Operation["Transmit Backend"]="si5351";
            draft.Si5351["Adapter Identity"]=testIdentity;
            draft.Si5351["I2C Bus"]=1;
            draft.Si5351["I2C Address"]="0x60";
            sessionStorage.setItem(CONFIG_DRAFT_STORAGE_KEY,JSON.stringify({payload:draft}));
            suppressNextConfigDraftRestore=false;lastSavedConfigPayload="{}";
            if(!restorePersistedConfigDraft()) throw Error("persisted draft did not restore");
        `);
        assert.equal(await evaluate('i2cRequests.at(-1).options.data.bus'),22,'draft restore probes the resolved bus, not the saved legacy bus');
        assert.equal(await evaluate('document.getElementById("si5351_i2c_bus").numericBus'),1,'draft restore preserves separate numeric selection');
        await evaluate('i2cRequests.at(-1).done({"I2C Bus":22,Addresses:["0x60"],"Discovery Error":""})');
        const beforeInvalidAddress = await evaluate('i2cRequests.length');
        await evaluate('refreshSi5351Addresses(22,"invalid")');
        assert.equal(await evaluate('i2cRequests.length'),beforeInvalidAddress,'invalid address never defaults to 0x60 for a stable adapter');
        // Old success cannot replace the current draft after an identity change.
        await evaluate('refreshSi5351Addresses(22,"0x60");globalThis.oldReply=i2cRequests.at(-1);loadAdapters([testBus(22,"usb:04d8:00dd:serial:6e6577")],"usb:04d8:00dd:serial:6e6577");oldReply.done({"I2C Bus":22,Addresses:["0x60"],"Discovery Error":""})');
        assert.equal(await evaluate('selectedAdapterIdentity()'),'usb:04d8:00dd:serial:6e6577');
        await evaluate('loadAdapters([testBus(22)],"");populateI2cBuses(22,"")');
        assert.equal(await evaluate('Number.isNaN(selectedI2cBusValue())'),true,'MCP numeric selection requires deliberate migration');
        await evaluate('loadAdapters([{Number:1,Name:"Generic I2C",Identity:""}],"")');
        assert.equal(await evaluate('selectedI2cBusValue()'),1,'generic numeric configs remain valid');
        const states=[
            ['one',[{Number:22,Name:'MCP2221A',Identity:'usb:04d8:00dd:serial:6164616672756974'}]],
            ['permission',[{Number:22,Name:'MCP2221A',Identity:'usb:04d8:00dd:serial:6164616672756974'}]],
            ['address-missing',[{Number:22,Name:'MCP2221A',Identity:'usb:04d8:00dd:serial:6164616672756974'}]],
            ['none',[]],
            ['multiple',[{Number:22,Name:'MCP2221A',Identity:'usb:04d8:00dd:serial:6164616672756974'},{Number:23,Name:'MCP2221A',Identity:'usb:04d8:00dd:serial:6164616672756974'}]],
        ];
        for (const [device,width,height] of [['desktop',1440,1000],['mobile',390,844]]) {
            await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:device==='mobile'});
            for (const theme of ['light','dark']) for(const [state,buses] of states) {
                await evaluate(`document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});loadAdapters(${JSON.stringify(buses)});if(${JSON.stringify(state)}==='one'){populateSi5351Addresses(22,'0x60',['0x60'],'',22);}if(${JSON.stringify(state)}==='permission'){populateSi5351Addresses(22,'0x60',[],'Permission denied; check I2C access.',22);}if(${JSON.stringify(state)}==='address-missing'){populateSi5351Addresses(22,'0x61',[],'',22);}document.getElementById('si5351-backend-panel').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-100)`);
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,`${device} horizontal overflow`);
                if(evidence) {
                    const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
                    fs.writeFileSync(path.join(evidence,`${device}-${theme}-${state}.png`),shot.data,'base64');
                }
            }
        }
        // The same shipped controls must also handle a native host without a
        // numeric device node or automatic selection of an identical adapter.
        await evaluate(`
            window.WSPRRYPI_PLATFORM.si5351Transport='mcp2221a-hid';
            window.WSPRRYPI_PLATFORM.si5351AdapterDiscoveryError='';
            globalThis.nativeAdapter={Identity:testIdentity,Name:'MCP2221A',Available:true};
            window.WSPRRYPI_PLATFORM.si5351Adapters=[nativeAdapter];
            populateI2cBuses(17,testIdentity);populateSi5351Addresses(17,'0x60',['0x60'],'',17);
            updateMachinePowerCapabilities({reboot:false,power_off:false});
        `);
        assert.equal(await evaluate('selectedI2cBusValue()'),17);
        assert.equal(await evaluate('document.getElementById("si5351_i2c_bus").numericBus'),17);
        assert.equal(await evaluate('document.getElementById("rebootButton").disabled && document.getElementById("shutdownButton").disabled'),true);
        assert.match(await evaluate('document.getElementById("machine-power-status").textContent'),/unavailable/);
        assert.equal(await evaluate('document.getElementById("si5351-bus-hint").textContent.includes("/dev/i2c")'),false);
        await evaluate(`document.getElementById('si5351_i2c_address').value='0x61';document.getElementById('si5351_i2c_address').dispatchEvent(new Event('change',{bubbles:true}));`);
        assert.equal(await evaluate('i2cRequests.at(-1).options.data.identity'),'usb:04d8:00dd:serial:6164616672756974');
        assert.equal(await evaluate('i2cRequests.at(-1).options.data.address'),97);
        await evaluate(`i2cRequests.at(-1).done({'I2C Bus':17,'Adapter Identity':testIdentity,Transport:'mcp2221a-hid',Addresses:['0x61'],'Discovery Error':''})`);
        assert.equal(await evaluate('document.getElementById("si5351_i2c_address").value'),'0x61');
        for (const [device,width,height] of [['desktop',1440,1000],['mobile',390,844]]) {
            await client.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:device==='mobile'});
            for (const theme of ['light','dark']) for(const state of ['available','denied','missing','duplicate']) {
                await evaluate(`
                    document.documentElement.setAttribute('data-bs-theme',${JSON.stringify(theme)});
                    window.WSPRRYPI_PLATFORM.si5351Adapters=${JSON.stringify(state)}==='missing'?[]:${JSON.stringify(state)}==='duplicate'?[nativeAdapter,nativeAdapter]:[nativeAdapter];
                    populateI2cBuses(17,testIdentity);
                    if(['available','denied'].includes(${JSON.stringify(state)}))populateSi5351Addresses(17,'0x60',${JSON.stringify(state)}==='available'?['0x60']:[],${JSON.stringify(state)}==='denied'?'HID access denied. Close the other application and retry.':'',17);
                    document.getElementById('si5351-backend-panel').scrollIntoView({block:'start',behavior:'instant'});window.scrollBy(0,-100);
                `);
                assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,`${device} native overflow`);
                if(evidence) {const shot=await client.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});fs.writeFileSync(path.join(evidence,`native-${device}-${theme}-${state}.png`),shot.data,'base64');}
            }
        }
        console.log('Native host browser: retained numeric setting, explicit adapter/address recovery, unavailable machine-power controls, and desktop/mobile light/dark inventory states passed');
        console.log('Linux I2C UI: no/one/multiple adapters, reconnect renumbering, permission error, identity-change late reply, legacy numeric persistence, and desktop/mobile light/dark passed');
    } finally {
        client?.close();
        if(chromium) await stopIsolatedProcess(chromium);
        if(php) await stopChild(php);
    }
}
main().catch(error=>{console.error(error);process.exitCode=1});
