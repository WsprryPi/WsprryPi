"use strict";

const assert = require("node:assert/strict");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");
const fs = require("node:fs");
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

// Runs the real Maintenance modal and production handlers with an in-memory
// controller. This fixture never opens a transmitter/control connection.
async function browserTest() {
    const ok = (value, text) => { if (!value) throw new Error(text); };
    const node = id => document.getElementById(id);
    const event = { preventDefault() {} };
    clearPendingTestToneStartRequest(); clearPendingTestToneEndTimeout();
    clearWebSocketReconnectTimer(); clearWsprBandCatalogPendingRequest();
    currentRuntimeStatus = null;
    currentRuntimeConfigStatus = { mode: "WSPR", transmitEnabled: false };
    getTxState = () => {};
    ws = { readyState: WebSocket.OPEN, sent: [], send(text) { this.sent.push(JSON.parse(text)); } };
    websocketCurrentlyConnected = true;
    wsprBandCatalogAuthorized = true;
    wsprBandCatalogConnectionGeneration += 1;
    const bands = ["2200m", "630m", "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m", "4m", "2m", "1.25m", "70cm"];
    const dials = [136000,474200,1836600,3568600,5287200,7038600,10138700,14095600,18104600,21094600,24924600,28124600,50293000,70091000,144489000,222100000,432300000];
    const catalog = { command: "wspr_band_catalog", status: "ok", audio_offset_hz: 1500,
        frequency_profile: "existing_common", band_preferences: {},
        bands: bands.map((band, i) => ({ band, dial_frequency_hz: dials[i], tone_frequency_hz: dials[i]+1500, resolution_source: "built_in_preset", preset: band })),
        presets: bands.map((band, i) => ({ band, preset: band, dial_frequency_hz: dials[i], existing_common: true })) };
    ok(updateWsprBandCatalog(catalog), "catalog fixture valid");
    bindTestToneControls();
    initializeTestToneSelectionControls();
    node("testToneSourceCustom").checked = true;
    node("testToneSourceBand").checked = false;
    node("testToneFrequencyHz").value = "14097123";
    renderTestToneSelection();
    const modal = bootstrap.Modal.getOrCreateInstance(node("testToneModal")); modal.show();
    await new Promise(resolve => node("testToneModal").addEventListener("shown.bs.modal", resolve, { once: true }));
    const preview = node("testToneSelectionPreview").textContent;
    onTestToneStart.call(node("testToneStart"), event);
    ok(testToneLifecycleState === "start_pending", "Start pending");
    ok(node("testToneExecutionResult").textContent.includes("awaiting acceptance"), "pending announced");
    const startId = ws.sent.at(-1).client_request_id;
    const status = (revision, state, owned = true, message = "", restoration = "none") => ({ generation: 2, revision, state, owned, message, restoration });
    const accepted = { command: "tone_start", client_request_id: startId, started: true,
        frequency_source: "custom_rf", band: "20m", actual_rf_frequency_hz: 14097123,
        selector_gpio_enabled: true, selector_gpio: 17, selector_gpio_active_high: false,
        test_tone: status(1, "starting") };
    handleTestToneCommandResponse(accepted);
    ok(testToneLifecycleState === "starting", "acceptance is starting");
    ok(node("testToneExecutionResult").textContent.includes("GPIO 17, active low"), "selector shown");
    reconcileTestToneStatus(status(2, "active"));
    ok(testToneLifecycleState === "active", "runtime confirms active");
    const active = node("testToneExecutionResult").textContent;
    handleTestToneCommandResponse(accepted);
    handleTestToneCommandResponse({ command: "tone_start", started: false, message: "old rejection" });
    ok(node("testToneExecutionResult").textContent === active, "stale/duplicate results preserve active evidence");
    onTestToneEnd.call(node("testToneEnd"), event);
    ok(testToneLifecycleState === "end_pending", "End pending");
    reconcileTestToneStatus(status(3, "active"));
    ok(testToneLifecycleState === "end_pending" && node("testToneEnd").disabled,
       "runtime observation preserves pending End and prevents duplicate submission");
    handleTestToneCommandResponse({ command: "tone_end", client_request_id: ws.sent.at(-1).client_request_id,
        stopped: true, message: "Test Tone ended; scheduler configuration restored.",
        test_tone: status(4, "ended", false, "Test Tone ended; scheduler configuration restored.", "scheduler") });
    ok(node("testToneExecutionResult").textContent.includes("scheduler configuration restored"), "End restoration preserved");
    ok(node("testToneSelectionPreview").textContent === preview, "preview independent of outcome");
    node("testToneFrequencyHz").value = "broken"; renderTestToneSelection();
    ok(node("testToneStart").disabled && node("testToneSelectionError").textContent, "invalid draft preserved");
    node("testToneFrequencyHz").value = "14097123"; renderTestToneSelection();
    onTestToneStart.call(node("testToneStart"), event);
    clearPendingTestToneStartRequest(false);
    quarantineTimedOutTestToneStartRequest();
    wsprBandCatalogAuthorized = false; renderTestToneSelection();
    ok(!node("testToneEnd").disabled && node("testToneStart").disabled, "End recovers without catalog");
    onTestToneEnd.call(node("testToneEnd"), event);
    handleTestToneCommandResponse({ command: "tone_end", client_request_id: ws.sent.at(-1).client_request_id,
        stopped: true, restoration_failed: true, status: "error",
        message: "Test Tone ended; direct tone restoration failed. Use local recovery.",
        test_tone: status(5, "unknown", false, "Test Tone ended; direct tone restoration failed. Use local recovery.", "failed") });
    ok(testToneLifecycleState === "unknown" && !node("testToneEnd").disabled, "restoration failure retains recovery");
    ok(node("testToneExecutionResult").getAttribute("role") === "status" &&
       node("testToneExecutionResult").getAttribute("aria-live") === "polite", "live announcements retained");
    node("testToneEnd").focus(); ok(document.activeElement === node("testToneEnd"), "keyboard recovery focus");
    // Exercise the production socket callbacks as well as direct handlers.
    class FixtureSocket {
        static OPEN = 1; static CONNECTING = 0;
        constructor() { this.readyState = 0; this.sent = []; this.listeners = {}; }
        addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
        emit(type, event = {}) { (this.listeners[type] || []).forEach(callback => callback(event)); }
        open() { this.readyState = 1; this.emit("open"); }
        message(value) { this.emit("message", { data: JSON.stringify(value) }); }
        close() { this.readyState = 3; this.emit("close", { code: 1006 }); }
        send(text) { this.sent.push(JSON.parse(text)); }
    }
    clearPendingTestToneStartRequest(); clearPendingTestToneEndTimeout();
    testToneLifecycleState = TEST_TONE_LIFECYCLE.IDLE;
    testToneLatestRevision = -1; testToneLatestStatus = null;
    window.WebSocket = FixtureSocket;
    ws = null; websocketCurrentlyConnected = false;
    communicationInterrupted = false; reloadAfterReconnectPending = false;
    reloadAllData = () => {};
    const oldSocket = connectWebSocket("ws://fixture"); oldSocket.open(); oldSocket.message(catalog);
    node("testToneSourceCustom").checked = true; node("testToneSourceBand").checked = false;
    node("testToneFrequencyHz").value = "14097123";
    currentRuntimeStatus = null;
    currentRuntimeConfigStatus = { mode: "WSPR", transmitEnabled: false };
    renderTestToneSelection(); onTestToneStart.call(node("testToneStart"), event);
    ok(testToneLifecycleState === "start_pending", "socket Start pending: " + JSON.stringify({ state:testToneLifecycleState, connected:websocketCurrentlyConnected, catalog:wsprBandCatalogAuthorized, selection:currentTestToneSelection }));
    oldSocket.close();
    ok(testToneLifecycleState === "unknown", "disconnect preserves possibly sent Start");
    clearWebSocketReconnectTimer();
    const reconnected = connectWebSocket("ws://fixture"); reconnected.open();
    ok(!node("testToneEnd").disabled, "reconnect retains End without catalog");
    reconnected.message(catalog);
    reconnected.message({ command: "test_tone_status", test_tone: status(20, "active") });
    const newer = node("testToneExecutionResult").textContent;
    oldSocket.message({ command: "tone_end", stopped: true });
    reconnected.message({ command: "tone_start", started: false, message: "passive rejection" });
    ok(node("testToneExecutionResult").textContent === newer, "stale socket and passive rejection preserve current evidence");
    onTestToneEnd.call(node("testToneEnd"), event);
    reconnected.message({ command: "tone_end", client_request_id: reconnected.sent.at(-1).client_request_id,
        stopped: false, message: "Cleanup remains uncertain; use End to recover.",
        test_tone: status(21, "unknown") });
    ok(testToneLifecycleState === "unknown" && !node("testToneEnd").disabled, "failed End remains recovery, never active certainty");
    const providerDetail = "RP1 GPCLK GET_STATE refused: Input/output error. Cleanup remains unconfirmed. Use End to retry cleanup. <provider-detail>";
    const providerDraft = node("testToneFrequencyHz").value;
    reconnected.message({ command: "test_tone_status",
        test_tone: status(22, "unknown", false, providerDetail, "failed") });
    ok(node("testToneExecutionResult").textContent.includes(providerDetail), "provider refusal detail remains visible");
    ok(!node("testToneExecutionResult").querySelector("provider-detail"), "provider detail is rendered as text");
    ok(node("testToneFrequencyHz").value === providerDraft && !node("testToneEnd").disabled,
       "provider refusal preserves the operator draft and cleanup recovery: " + JSON.stringify({
           before: providerDraft, after: node("testToneFrequencyHz").value, endDisabled: node("testToneEnd").disabled }));
    clearWebSocketReconnectTimer();
    window.toneVisual = { status, accepted };
    return { assertions: "passed", workflows: 17 };
}

async function main() {
    const phpPort = await freePort(); const debugPort = await freePort();
    const profileDir = `/tmp/wsprrypi-tone-browser-${process.pid}`;
    const evidence = process.env.TEST_TONE_EVIDENCE_DIR || "/tmp/wsprrypi-tone-ui-evidence";
    fs.mkdirSync(evidence, { recursive: true });
    let chromium, client, php, failure;
    let escapedRequests = 0;
    const sentinel = http.createServer((request, response) => {
        escapedRequests += 1;
        response.end("No real application is attached to this sentinel.");
    });
    try {
        await new Promise(resolve => sentinel.listen(0, "127.0.0.1", resolve));
        php = spawnFixtureChild("php", ["-S", `127.0.0.1:${phpPort}`, "-t", "data", "tests/browser_fixture_router.php"], { cwd: UI_ROOT });
        await waitForSpawn(php);
        await waitFor(async () => await getStatus(`http://127.0.0.1:${phpPort}/index.php?page=maintenance`) === 200, "PHP fixture");
        chromium = spawnIsolatedProcess(process.env.CHROME_BIN || (process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "chromium"),
            ["--headless", "--no-sandbox", "--disable-gpu", `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profileDir}`, "about:blank"]);
        await waitForSpawn(chromium);
        const page = await waitFor(async () => (await getJson(`http://127.0.0.1:${debugPort}/json`)).find(item => item.type === "page"), "Chromium");
        client = new CdpClient(page.webSocketDebuggerUrl); await client.open();
        await client.send("Page.enable");
        // Disable real control transports before any production script runs.
        await client.send("Page.addScriptToEvaluateOnNewDocument", { source: `
            window.WebSocket = class { static OPEN=1; static CONNECTING=0; readyState=0;
                addEventListener() {} send() { throw new Error('fixture transport only'); } close() {} };
            localStorage.setItem('wsprrypi.updateCheckDisabled', 'true');
        ` });
        await client.send("Page.navigate", { url: `http://127.0.0.1:${phpPort}/index.php?page=maintenance` });
        await waitFor(async () => {
            const r = await client.send("Runtime.evaluate", { expression: "typeof onTestToneStart === 'function' && typeof bootstrap === 'object' && document.readyState === 'complete'", returnByValue: true });
            return r.result.value;
        }, "Maintenance scripts");
        // Prove the browser enforces isolation before installing in-memory
        // workflow mocks. The foreign port is an owned harmless sentinel.
        const isolation = await client.send("Runtime.evaluate", { expression: `(async () => {
            const foreign = 'http://127.0.0.1:${sentinel.address().port}/config';
            const fetchBlocked = await fetch(foreign).then(() => false, () => true);
            const xhrBlocked = await new Promise(resolve => {
                const request = new XMLHttpRequest();
                request.onerror = () => resolve(true); request.onload = () => resolve(false);
                request.open('GET', foreign); request.send();
            });
            return {fetchBlocked, xhrBlocked, config: (await fetch('/config')).status,
                logs: (await fetch('/log_stream.php')).status,
                spots: (await fetch('/fetch_spots.php')).status};
        })()`, awaitPromise: true, returnByValue: true });
        assert.ok(!isolation.exceptionDetails, JSON.stringify(isolation.exceptionDetails));
        assert.deepEqual(isolation.result.value,
            {fetchBlocked: true, xhrBlocked: true, config: 503, logs: 503, spots: 503});
        assert.equal(escapedRequests, 0, "no request reaches the other-port application sentinel");
        console.log("Browser fixture isolation: CSP blocks direct fallback; application, journal and spot endpoints refuse execution");
        const run = await client.send("Runtime.evaluate", { expression: `(${browserTest.toString()})()`, awaitPromise: true, returnByValue: true });
        if (run.exceptionDetails) throw new Error(run.exceptionDetails.exception?.description || run.exceptionDetails.text);
        assert.deepEqual(run.result.value, { assertions: "passed", workflows: 17 });
        const inputFocus = await client.send("Runtime.evaluate", { expression: `
            document.getElementById('testToneSourceCustom').checked=true;
            document.getElementById('testToneSourceBand').checked=false;
            document.getElementById('testToneFrequencyHz').value='14097123';
            renderTestToneSelection();
            document.getElementById('testToneFrequencyHz').focus(); document.activeElement.id`, returnByValue: true });
        assert.equal(inputFocus.result.value, "testToneFrequencyHz", "custom draft accepts keyboard focus");
        await client.send("Input.dispatchKeyEvent", { type: "keyDown", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9 });
        await client.send("Input.dispatchKeyEvent", { type: "keyUp", key: "Tab", code: "Tab", windowsVirtualKeyCode: 9 });
        const focus = await client.send("Runtime.evaluate", { expression: "document.activeElement.id", returnByValue: true });
        assert.equal(focus.result.value, "testToneEnd", "keyboard Tab reaches End recovery");
        const measurements = [];
        for (const theme of ["light", "dark"]) for (const viewport of [
            { name: "desktop", width: 1440, height: 1000 }, { name: "mobile", width: 390, height: 844 },
        ]) {
            await client.send("Emulation.setDeviceMetricsOverride", { ...{ width: viewport.width, height: viewport.height }, deviceScaleFactor: 1, mobile: viewport.name === "mobile" });
            for (const [name, expression] of [
                ["accepted", `testToneLatestRevision=-1; reconcileTestToneStatus(toneVisual.status(10,'starting'));`],
                ["active-difference", `testToneLatestRevision=-1; testToneCommittedDetails='Committed on 20m at exact 14.097100 MHz RF (different from the request). Selector: GPIO 17, active low.'; reconcileTestToneStatus(toneVisual.status(11,'active'));`],
                ["restored", `testToneLatestRevision=-1; reconcileTestToneStatus(toneVisual.status(12,'ended',false,'Test Tone ended; previous CLI direct tone restored (startup requested).','direct_tone'));`],
                ["recovery", `testToneLatestRevision=-1; reconcileTestToneStatus(toneVisual.status(13,'unknown',false,'Test Tone ended; direct tone restoration failed. Output cleanup is unconfirmed. Use End or local recovery.','failed'));`],
                ["provider-refusal", `testToneLatestRevision=-1; reconcileTestToneStatus(toneVisual.status(14,'unknown',false,'RP1 GPCLK GET_STATE refused: Input/output error. Cleanup remains unconfirmed. Use End to retry cleanup.','failed'));`],
            ]) {
                const r = await client.send("Runtime.evaluate", { expression: `(() => { document.documentElement.setAttribute('data-bs-theme', '${theme}'); ${expression}
                    const modal=document.getElementById('testToneModal'); const result=document.getElementById('testToneExecutionResult');
                    const rect=modal.querySelector('.modal-content').getBoundingClientRect();
                    return {theme:'${theme}', viewport:'${viewport.name}', state:'${name}', width:innerWidth,
                        overflow:document.documentElement.scrollWidth>innerWidth, modalWidth:rect.width,
                        resultOverflow:result.scrollWidth>result.clientWidth, live:result.getAttribute('aria-live'),
                        color:getComputedStyle(result).color, font:getComputedStyle(result).fontSize,
                        endDisabled:document.getElementById('testToneEnd').disabled}; })()`, returnByValue: true });
                assert.ok(!r.exceptionDetails, JSON.stringify(r.exceptionDetails));
                const metrics = r.result.value;
                assert.equal(metrics.overflow, false, JSON.stringify(metrics));
                assert.equal(metrics.resultOverflow, false, JSON.stringify(metrics));
                assert.equal(metrics.live, "polite"); measurements.push(metrics);
                const shot = await client.send("Page.captureScreenshot", { format: "png" });
                fs.writeFileSync(path.join(evidence, `${theme}-${viewport.name}-${name}.png`), Buffer.from(shot.data, "base64"));
            }
        }
        // 200% text enlargement is a layout stress check in the same viewports.
        const zoom = await client.send("Runtime.evaluate", { expression: "document.documentElement.style.fontSize='200%'; ({overflow:document.documentElement.scrollWidth>innerWidth,end:document.getElementById('testToneEnd').getBoundingClientRect().width})", returnByValue: true });
        assert.equal(zoom.result.value.overflow, false);
        await client.send("Runtime.evaluate", { expression: "document.getElementById('testToneEnd').focus(); document.getElementById('testToneEnd').scrollIntoView({block:'center'});" });
        const shot = await client.send("Page.captureScreenshot", { format: "png" });
        fs.writeFileSync(path.join(evidence, "dark-mobile-200-percent.png"), Buffer.from(shot.data, "base64"));
        fs.writeFileSync(path.join(evidence, "measurements.json"), JSON.stringify({ run: run.result.value, measurements, textZoom: zoom.result.value }, null, 2));
        console.log(`Test Tone browser: 17 workflows passed; 21 screenshots in ${evidence}`);
        await require("./integrated_review_browser.js").reviewIntegratedUi(client, `http://127.0.0.1:${phpPort}`, path.join(evidence, "integrated"));
    } catch (error) { failure = error; }
    finally {
        const errors = failure ? [failure] : [];
        if (client && chromium?.exitCode === null) {
            await Promise.race([client.send("Browser.close").catch(() => {}), new Promise(resolve => setTimeout(resolve, 1000))]);
        }
        for (const cleanup of [() => client?.close(), () => stopIsolatedProcess(chromium), () => stopChild(php),
            () => new Promise((resolve, reject) => sentinel.close(error => error ? reject(error) : resolve())),
            () => fs.rmSync(profileDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 })]) {
            try { await cleanup(); } catch (error) { if (!errors.includes(error)) errors.push(error); }
        }
        if (errors.length) throw new AggregateError(errors, "Test Tone browser workflow or cleanup failed");
    }
}
main().catch(error => { console.error(error); process.exitCode=1; });
