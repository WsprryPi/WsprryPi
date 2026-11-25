// SPDX-License-Identifier: MIT
// The zero-output production main and separate web service use isolated SQLite
// state and loopback listeners. The harness supplies PHP and the same-origin proxy.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const http = require("node:http");
const net = require("node:net");
const WebSocket = require("ws");
const {waitForPhp, createBrowserProfile, freePort, waitFor, launchBrowser, finishFixture,
    spawnFixtureChild, waitForSpawn, closeServer, getJson, stopChild} = require("./browser_harness.js");
const {waitForExit, childDiagnostic} = require("./browser_fixture_process.js");

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
    const ui = path.resolve(__dirname, "..");
    const binary = process.env.WSPRRYPI_CONTROLLER_FIXTURE;
    assert.ok(binary && path.isAbsolute(binary), "Explicit controller-profile production fixture required");
    const backend = await backendPort(39100), socket = await backendPort(backend + 1), phpPort = await freePort();
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "controller-ui-"));
    const ledger = path.join(directory, "ledger"), state = path.join(directory, "state");
    const requests = [], upgrades = [];
    let application, web, php, proxy, chrome, client, profile, failure;
    let versionMode = "unauthorized";
    try {
        const initialize = spawnFixtureChild(binary, [ledger, "--initialize-state", "--controller-only", "--state-directory", state]);
        assert(await waitForExit(initialize, 10000), childDiagnostic(initialize));
        assert.equal(initialize.exitCode, 0, childDiagnostic(initialize));
        application = spawnFixtureChild(binary, [ledger, "--controller-only", "--state-directory", state,
            "--web-port", String(backend), "--socket-port", String(socket), "--socket-loopback-only"]);
        web = spawnFixtureChild(binary, [ledger, "--web-service", "--listener-cache", path.join(directory, "listeners.json")]);
        await Promise.all([waitForSpawn(application), waitForSpawn(web)]);
        await waitFor(async () => (await getJson(`http://127.0.0.1:${backend}/status`)).controller_ready, "controller ready", 20000);
        const actualStatus = await getJson(`http://127.0.0.1:${backend}/status`);
        const actualVersion = await getJson(`http://127.0.0.1:${backend}/version`);
        assert.deepEqual(actualVersion.host_status, actualStatus);
        assert.equal(actualVersion.compiled_backends, "");
        assert.equal(actualVersion.ancillary_gpio, false);
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${phpPort}`, "-t", "data", "tests/browser_fixture_router.php"], {
            cwd: ui, env: {...process.env, WSPRRYPI_UI_FIXTURE_PREFIX: "/wsprrypi"}
        });
        await waitForSpawn(php);
        await waitForPhp(php, `http://127.0.0.1:${phpPort}/wsprrypi/index.php?page=config`, "controller PHP templates");
        proxy = http.createServer((request, response) => {
            const url = new URL(request.url, "http://fixture");
            const route = url.pathname.replace(/^\/wsprrypi(?=\/)/, "");
            const version = route === "/version";
            const production = version || route === "/status" || route.startsWith("/config") ||
                route.startsWith("/api/") || route.startsWith("/control/");
            requests.push({method: request.method, route});
            if (version && versionMode !== "upstream") {
                const status = versionMode === "unauthorized" ? 403 : 200;
                const host_status = versionMode === "changed" ? {...actualStatus, device_id: "f".repeat(32)}
                    : versionMode === "starting" ? {...actualStatus, controller_ready: false} : {role: "controller-only"};
                response.writeHead(status, {"Content-Type": "application/json"});
                response.end(JSON.stringify({...actualVersion, host_status}));
                return;
            }
            const outbound = http.request({hostname: "127.0.0.1", port: production ? backend : phpPort,
                method: request.method, path: production ? route + url.search : request.url, headers: request.headers}, incoming => {
                response.writeHead(incoming.statusCode, incoming.headers);
                incoming.pipe(response);
            });
            outbound.on("error", () => {response.writeHead(503); response.end();});
            request.pipe(outbound);
        });
        proxy.on("upgrade", (request, downstream) => {
            upgrades.push(request.url);
            downstream.destroy();
        });
        await new Promise(resolve => proxy.listen(0, "127.0.0.1", resolve));
        const origin = `http://127.0.0.1:${proxy.address().port}`;
        const base = origin + "/wsprrypi/";
        profile = createBrowserProfile();
        ({browser: chrome, client} = await launchBrowser({executable: process.env.CHROME_BIN ||
            (process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "chromium"),
            args: ["--headless", "--no-sandbox", "--disable-gpu", "--no-first-run", "--disable-background-networking", "about:blank"],
            profile, timeoutMs: 60000}));
        const run = expression => client.evaluate(expression);
        const output = process.env.WSPRRYPI_CONTROLLER_SCREENSHOTS || path.join(directory, "screenshots");
        fs.mkdirSync(output, {recursive: true});
        async function capture(scene) {
            for (const theme of ["light", "dark"]) {
                for (const viewport of [{name: "desktop", width: 1440, height: 1050}, {name: "mobile", width: 390, height: 844}]) {
                    await client.send("Emulation.setDeviceMetricsOverride", {...viewport, deviceScaleFactor: 1, mobile: viewport.name === "mobile"});
                    await run(`applyThemePreference('${theme}');window.scrollTo({top:0,behavior:'instant'})`);
                    await client.settleRendering();
                    assert(await run("document.documentElement.scrollWidth <= innerWidth"), "No page overflow");
                    if (scene === "unauthorized") assert(await run(`(() => {
                        const button = document.getElementById('hostRoleRetry');
                        const notice = document.getElementById('controllerOnlyNotice');
                        const luminance = color => color.match(/\\d+/g).slice(0,3).map(Number).map(value => {
                            const v=value/255;
                            return v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4;
                        }).reduce((sum,value,index)=>sum+value*[0.2126,0.7152,0.0722][index],0);
                        const values=[luminance(getComputedStyle(button).color),luminance(getComputedStyle(notice).backgroundColor)];
                        return (Math.max(...values)+0.05)/(Math.min(...values)+0.05)>=4.5;
                    })()`), "Retry label meets text contrast in both themes");
                    const shot = await client.send("Page.captureScreenshot", {format: "png"});
                    fs.writeFileSync(path.join(output, `${scene}-${theme}-${viewport.name}.png`), Buffer.from(shot.data, "base64"));
                }
            }
        }
        const navigate = page => client.send("Page.navigate", {url: base + page});
        await navigate("index.php?page=config&setup_tab=%23radio-pane");
        await waitFor(() => run("document.getElementById('controllerOnlyNotice')?.textContent.includes('not authorized')"), "Unauthorized bootstrap");
        assert(await run("document.getElementById('setup-device').disabled && !document.getElementById('wsprform').getClientRects().length"));
        assert(await run("!document.getElementById('hostRoleRetry').disabled"), "Retry works while role-dependent controls are held");
        await capture("unauthorized");
        versionMode = "invalid";
        await run("document.getElementById('hostRoleRetry').focus()");
        assert.equal(await run("document.activeElement.id"), "hostRoleRetry");
        await client.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Enter", code: "Enter"});
        await client.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Enter", code: "Enter"});
        await waitFor(() => run("document.getElementById('controllerOnlyNotice').textContent.includes('could not be verified')"), "Malformed role refusal");
        versionMode = "starting";
        await run("ControllerMode.refresh()");
        await waitFor(() => run("ControllerMode.active && !ControllerMode.ready"), "Controller starting");
        assert(await run("document.querySelector('.fleet-catalog').inert"));
        versionMode = "upstream";
        await run("document.getElementById('hostRoleTitle').scrollIntoView();ControllerMode.refresh()");
        await waitFor(() => run("window.ControllerMode?.ready && WtpUi.catalogReady && document.getElementById('fleet-tab').classList.contains('active') && getComputedStyle(document.getElementById('fleet-pane')).opacity === '1' "), "Production controller Setup");
        assert.equal(await run("document.getElementById('controllerHostIdentity').textContent"), "Controller ID " + actualStatus.device_id);
        assert(await run("document.getElementById('radio-tab').closest('li').hidden && document.getElementById('wsprform').inert && !document.getElementById('fleet-add').disabled"));
        await run("document.getElementById('callsign').value='UNSAVED';scheduleAutosave();flushAutosave();patchTransmitControl();stopTransmission();populateConfig()");
        assert.equal(await run("document.getElementById('callsign').value"), "UNSAVED");
        await run("document.getElementById('fleet-add').focus()");
        await client.send("Input.dispatchKeyEvent", {type: "keyDown", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9});
        await client.send("Input.dispatchKeyEvent", {type: "keyUp", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9});
        assert(await run("document.activeElement !== document.body && document.activeElement.matches(':focus-visible')"));
        await capture("setup");
        await navigate("index.php?page=operation");
        await waitFor(() => run("window.ControllerMode?.ready && document.getElementById('operationControllerHeading').textContent === 'No managed devices'"), "Verified empty Operation");
        assert(await run("document.getElementById('operationDevice').value === '' && !document.getElementById('operationLocalView').getClientRects().length && !document.getElementById('rebootButton').getClientRects().length"));
        await capture("operation");
        await navigate("maintenance.php");
        await waitFor(() => run("window.ControllerMode?.ready && !document.getElementById('controllerAccess').inert"), "Controller Maintenance deep link");
        assert(await run("['repairConfigButton','restoreConfigButton','networkSafetyPanel','test_tone','supportBundlePanel'].every(id => !document.getElementById(id).getClientRects().length)"));
        assert(await run("!!document.getElementById('backupRestore').getClientRects().length"));
        await capture("maintenance");
        await navigate("view_logs.php?view=journal");
        await waitFor(() => run("window.ControllerMode?.ready && !!document.querySelector('[data-controller-only]').getClientRects().length"), "Controller Logs deep link");
        await navigate("view_spots.php");
        await waitFor(() => run("window.ControllerMode?.ready && !!document.querySelector('[data-controller-only]').getClientRects().length"), "Controller Spots deep link");
        await navigate("index.php?page=config");
        await waitFor(() => run("window.ControllerMode?.ready && WtpUi.catalogReady"), "Controller Setup reload");
        versionMode = "changed";
        await run("ControllerMode.refresh()");
        await waitFor(() => run("!ControllerMode.ready && document.querySelector('.fleet-catalog').inert && document.getElementById('controllerOnlyNotice').textContent.includes('identity or role changed')"), "Replacement host fenced");
        assert.equal(upgrades.length, 0, "Controller browser must not open the local transmitter WebSocket");
        const localOnly = /^(?:\/config(?:\/|$)|\/control\/stop|\/log_stream\.php|\/fetch_spots\.php|\/api\/(?:wtp|network-safety|rp1-gpclk-route|support-bundles)(?:\/|$)|\/api\/control\/v1\/(?:status|host\/(?:readiness|wtp-endpoint|config|logging|udp-logging)|member\/firmware)(?:\/|$))/;
        assert.deepEqual(requests.filter(item => localOnly.test(item.route)), [], "Startup and deep links must not probe local APIs");
        assert.deepEqual(requests.filter(item => !["GET","HEAD"].includes(item.method) && ![
            "/api/control/v1/host/management-session", "/api/recovery/session"
        ].includes(item.route)), [], "Opening controller pages must not write or enable anything");

        // Send protected direct requests only after proving browser startup did not send them.
        const refusedRoutes = [
            ["GET", "/config"], ["PUT", "/config"], ["PATCH", "/config"],
            ["POST", "/config/repair"], ["GET", "/config/si5351-addresses"], ["POST", "/control/stop"],
            ["GET", "/api/network-safety"], ["POST", "/api/network-safety"], ["POST", "/api/rp1-gpclk-route"],
            ["GET", "/api/wtp"], ["POST", "/api/wtp/recover"], ["GET", "/api/support-bundles"],
            ["GET", "/api/control/v1/host/wtp-endpoint"], ["POST", "/api/control/v1/host/wtp-endpoint/enable"],
            ["GET", "/api/control/v1/member/firmware"], ["POST", "/api/control/v1/member/firmware"]
        ];
        for (const [method, route] of refusedRoutes) {
            const body = ["GET", "HEAD"].includes(method) ? undefined : "{}";
            const response = await fetch(base + route.slice(1), {method, body, headers: {Origin: origin, "Content-Type": "application/json", "X-WsprryPico-Request": "1"}});
            assert.equal(response.status, 409, `${method} ${route}: ${await response.clone().text()}`);
            assert.equal((await response.json()).error.code, "controller_only", `${method} ${route}`);
        }
        const deniedOrigin = await fetch(base + "api/control/v1/targets", {headers: {Origin: "http://untrusted.invalid"}});
        assert.equal(deniedOrigin.status, 403);
        const connection = new WebSocket(`ws://localhost:${socket}/`, {
            handshakeTimeout: 5000, autoSelectFamily: true
        });
        try {
            await new Promise((resolve, reject) => {connection.once("open", resolve);connection.once("error", reject);});
            for (const command of ["get_tx_state", "stop", "tone_start", "tone_end", "bounded_tone", "shutdown", "reboot", "wspr_band_catalog", "echo"]) {
                const reply = await new Promise((resolve, reject) => {
                    const timer = setTimeout(() => reject(Error("No WebSocket response: " + command)), 5000);
                    connection.once("message", raw => {clearTimeout(timer); resolve(JSON.parse(raw));});
                    connection.send(JSON.stringify({command}));
                });
                assert.equal(reply.command, command);
                if (command === "get_tx_state") assert.equal(reply.local_output, null);
                else assert.equal(reply.error, "controller_only");
            }
        } finally { connection.terminate(); }
        console.log("PASS production controller bootstrap, refusal, keyboard, empty inventory, and desktop/mobile light/dark");
    } catch (error) {
        failure = error;
        if (client) try { console.error(await client.evaluate("document.body.innerText.slice(0,1000)")); } catch (_) {}
        for (const process of [application, web]) if (process) console.error(childDiagnostic(process));
    } finally {
        const outcomes = await Promise.allSettled([
            finishFixture({browser: chrome, client, php, profile, page: {}}),
            application ? stopChild(application) : Promise.resolve(),
            web ? stopChild(web) : Promise.resolve(),
            proxy ? closeServer(proxy) : Promise.resolve()
        ]);
        fs.rmSync(directory, {recursive: true, force: true});
        failure ||= outcomes.find(result => result.status === "rejected")?.reason;
    }
    if (failure) throw failure;
}

main().catch(error => {console.error(error);process.exitCode = 1;});
