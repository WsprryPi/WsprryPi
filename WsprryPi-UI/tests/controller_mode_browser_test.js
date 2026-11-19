// SPDX-License-Identifier: MIT
// Production templates and controller services. Only machine identity is injected
// by the C++ launcher; no selected backend, peer service, or RF device is used.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"),
    path = require("node:path"), http = require("node:http"), net = require("node:net");
const {waitForPhp, createBrowserProfile, freePort, waitFor, launchBrowser, finishFixture,
    spawnFixtureChild, waitForSpawn, closeServer, getJson, stopChild} = require("./browser_harness.js");

async function backendPort(start) {
    for (let port = start; port < start + 100; port++) {
        const server = net.createServer();
        const available = await new Promise(resolve => {
            server.once("error", () => resolve(false));
            server.listen(port, "127.0.0.1", () => server.close(() => resolve(true)));
        });
        if (available) return port;
    }
    throw Error("No controller fixture port available");
}
async function main() {
    const ui = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "controller-ui-"));
    const binary = process.env.WSPRRYPI_CONTROLLER_FIXTURE;
    assert.ok(binary && path.isAbsolute(binary), "Explicit compiled controller fixture required");
    const backend = await backendPort(39100), socket = await backendPort(backend + 1), phpPort = await freePort();
    const ini = path.join(directory,"controller.ini");
    fs.writeFileSync(ini, `[Operation]\nWeb Port = ${backend}\nSocket Port = ${socket}\n[Security]\nPrivileged Network Safety = enforced\n`, {mode:0o600});
    let application, php, proxy, chrome, client, profile, failure;
    const mutations = [], sockets = new Set();
    try {
        application = spawnFixtureChild(binary, ["--controller-only", "-i", ini]);
        await waitForSpawn(application);
        await waitFor(async () => (await getJson(`http://127.0.0.1:${backend}/status`)).controller_ready, "controller ready", 10000);
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${phpPort}`, "-t", "data", "tests/browser_fixture_router.php"], {cwd:ui});
        await waitForSpawn(php);await waitForPhp(php, `http://127.0.0.1:${phpPort}/index.php?page=config`, "controller PHP templates");
        proxy = http.createServer((request, response) => {
            const url = new URL(request.url, "http://fixture");
            const production = ["/config","/version"].includes(url.pathname) || url.pathname.startsWith("/api/control/v1/");
            if (production && !["GET","HEAD"].includes(request.method)) mutations.push({method:request.method,path:url.pathname});
            const outbound = http.request({hostname:"127.0.0.1",port:production?backend:phpPort,
                method:request.method,path:request.url,headers:request.headers}, upstream => {
                response.writeHead(upstream.statusCode,upstream.headers);upstream.pipe(response);
            });
            outbound.on("error", () => {response.writeHead(503);response.end();});
            request.pipe(outbound);
        });
        proxy.on("upgrade", (request, downstream, head) => {
            if (request.url !== "/socket") {downstream.destroy();return;}
            const upstream = net.connect(socket,"127.0.0.1");sockets.add(upstream);sockets.add(downstream);
            upstream.on("connect", () => {
                upstream.write(`${request.method} / HTTP/${request.httpVersion}\r\n` +
                    request.rawHeaders.reduce((text,value,index,all) => text+(index%2===0?`${value}: ${all[index+1]}\r\n`:""),"")+"\r\n");
                if (head.length) upstream.write(head);
                downstream.pipe(upstream).pipe(downstream);
            });
            upstream.on("error", () => downstream.destroy());downstream.on("error", () => upstream.destroy());
            upstream.on("close", () => {sockets.delete(upstream);downstream.destroy();});
            downstream.on("close", () => {sockets.delete(downstream);upstream.destroy();});
        });
        await new Promise(resolve => proxy.listen(0,"127.0.0.1",resolve));
        const origin = `http://127.0.0.1:${proxy.address().port}`;
        profile = createBrowserProfile();
        ({browser:chrome,client} = await launchBrowser({executable:process.env.CHROME_BIN ||
            (process.platform === "darwin"?"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome":"chromium"),
            args:["--headless","--no-sandbox","--disable-gpu","--no-first-run","--disable-background-networking","about:blank"],
            profile,timeoutMs:60000}));
        const run = expression => client.evaluate(expression);
        await client.send("Page.navigate", {url:origin+"/index.php?page=config"});
        await waitFor(() => run("!!window.ControllerMode?.ready && document.getElementById('fleet-tab').classList.contains('active') && getComputedStyle(document.getElementById('fleet-pane')).opacity==='1'"), "controller Setup role",15000);
        assert.equal(await run("document.getElementById('controllerOnlyNotice').hidden"),false);
        assert.equal(await run("document.getElementById('wtp_use')"),null,"Retired WTP backend selector is absent");
        assert.equal(await run("document.getElementById('transmit_backend').disabled"),true);
        assert.equal(await run("document.getElementById('radio-tab').disabled"),true);
        assert.equal(await run("document.getElementById('radio-pane').inert"),true);
        assert.equal(await run("document.getElementById('fleet-add').disabled"),false);
        assert.equal(await run("document.getElementById('fleet-listener')"),null,"Retired listener controls are absent");
        assert.match(await run("document.getElementById('fleet-schedules').textContent"),/read-only/);
        assert.equal(await run("document.getElementById('fleet-schedules').textContent.includes('local schedule remains available')"),false);
        await run("document.getElementById('callsign').value='UNSAVED DRAFT';scheduleAutosave();flushAutosave();patchTransmitControl();stopTransmission();WtpUi.refreshControls()");
        assert.equal(await run("document.getElementById('callsign').value"),"UNSAVED DRAFT");
        assert.equal(mutations.length,0,"controller role and reads must not write configuration");
        const output = process.env.WSPRRYPI_CONTROLLER_SCREENSHOTS || path.join(directory,"screenshots");
        fs.mkdirSync(output,{recursive:true});
        async function capture(scene) {
        for (const theme of ["light","dark"]) for (const viewport of [{name:"desktop",width:1440,height:1050},{name:"mobile",width:390,height:844}]) {
            await client.send("Emulation.setDeviceMetricsOverride",{...viewport,deviceScaleFactor:1,mobile:viewport.name==="mobile"});
            await run(`document.documentElement.setAttribute('data-bs-theme','${theme}');window.scrollTo(0,0)`);await client.settleRendering();
            await waitFor(() => run("window.scrollY===0"), "capture at top of page");
            assert.equal(await run("document.documentElement.scrollWidth<=window.innerWidth"),true);
            const shot = await client.send("Page.captureScreenshot",{format:"png",captureBeyondViewport:true});
            fs.writeFileSync(path.join(output,`${scene}-${theme}-${viewport.name}.png`),Buffer.from(shot.data,"base64"));
        }
        }
        await capture("setup");
        await run("PicoFirmwareSafety.begin();PicoFirmwareSafety.observeFirmware({maintenance:false,running:false})");
        await waitFor(() => run("!PicoFirmwareSafety.locked"), "firmware hold released");
        assert.equal(await run("document.getElementById('radio-tab').disabled && document.getElementById('transmit_backend').disabled"),true,
            "Firmware guard restoration must preserve controller restrictions");
        await run("ControllerMode.accept({role:'controller-only',controller_ready:true,device_id:'f'.repeat(32),local_output:null,local_transmission_available:false})");
        assert.equal(await run("ControllerMode.ready"),false);
        assert.equal(await run("document.querySelector('.fleet-catalog').inert"),true);
        assert.match(await run("document.getElementById('controllerOnlyNotice').textContent"),/identity changed/);
        assert.equal(await run("document.getElementById('callsign').value"),"UNSAVED DRAFT");
        await client.send("Page.navigate", {url:origin+"/index.php?page=operation"});
        await waitFor(() => run("!!window.ControllerMode?.ready && !!window.FleetOperation"), "controller Operations role",15000);
        assert.equal(await run("document.getElementById('operationLocalView').hidden"),true);
        assert.equal(await run("document.getElementById('operationControllerView').hidden"),false);
        await run("patchTransmitControl();patchRebootBehaviorControl();stopTransmission()");
        assert.equal(mutations.length,0);
        await capture("operation");
        console.log("PASS: production controller Setup/Operations, Fleet availability, local-action refusal, retained entries, and responsive themes");
    } catch(error) {failure = error;if(client)try{console.error(await client.evaluate("({ready:window.ControllerMode?.ready,active:window.ControllerMode?.active,safety:window.PicoFirmwareSafety?.locked,setup:!!window.SetupTargets,fleet:document.getElementById('fleet-tab')?.outerHTML,pane:document.getElementById('fleet-pane')?.className,opacity:document.getElementById('fleet-pane')&&getComputedStyle(document.getElementById('fleet-pane')).opacity,text:document.body.innerText.slice(0,500),form:document.getElementById('fleet-pane')?.closest('form')?.id})"));}catch(_){} }
    finally {
        for (const socket of sockets) socket.destroy();
        const outcomes = await Promise.allSettled([
            finishFixture({browser:chrome,client,php,profile,page:{}}),
            application ? stopChild(application) : Promise.resolve(),
            proxy ? closeServer(proxy) : Promise.resolve()
        ]);
        fs.rmSync(directory,{recursive:true,force:true});
        failure ||= outcomes.find(result => result.status === "rejected")?.reason;
    }
    if (failure) throw failure;
}
main().catch(error => {console.error(error);process.exitCode=1;});
