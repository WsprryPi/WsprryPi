"use strict";

// Transport and owned lifecycle only. Scenario policy stays in each consumer.
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const http = require("node:http");
const net = require("node:net");
const { performance } = require("node:perf_hooks");
const { AsyncLocalStorage } = require("node:async_hooks");
const WebSocket = require("ws");
const processFixture = require("./browser_fixture_process.js");
const { EventEmitter } = require("node:events");
const stages = new AsyncLocalStorage();
const profiles = new Map();
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

function diagnostic(stage, message, code = "HARNESS_FAILURE") {
    const error = new Error(`[${stage}] ${message}`);
    error.code = code;
    return error;
}

async function bounded(stage, budgetMs, operation) {
    const callSite = new Error().stack;
    const parent = stages.getStore();
    const start = performance.now();
    const deadline = Math.min(start + budgetMs, parent?.deadline ?? Infinity);
    if (parent?.signal.aborted) throw parent.signal.reason;
    if (deadline <= start) throw diagnostic(stage, "budget already exhausted", "HARNESS_TIMEOUT");
    const controller = new AbortController();
    const trace = parent?.trace || { lastObserved: "started" };
    trace.lastStage = stage;
    const abortParent = () => controller.abort(parent.signal.reason);
    parent?.signal.addEventListener("abort", abortParent, { once: true });
    let timer;
    try {
        return await stages.run({ stage, deadline, signal: controller.signal, trace }, () => new Promise((resolve, reject) => {
            controller.signal.addEventListener("abort", () => reject(controller.signal.reason), { once: true });
            timer = setTimeout(() => {
                const elapsedMs = Math.round(performance.now() - start);
                const timeout = diagnostic(stage, `expected completion; elapsed=${elapsedMs}ms budget=${budgetMs}ms; last transition=${trace.lastStage}; last observed=${trace.lastObserved}`, "HARNESS_TIMEOUT");
                timeout.stack += "\nStage entered at:\n" + callSite;
                Object.assign(timeout, { stage, elapsedMs, budgetMs, lastStage: trace.lastStage, lastObserved: trace.lastObserved });
                controller.abort(timeout);
            }, Math.max(0, deadline - performance.now()));
            Promise.resolve().then(() => operation(controller.signal)).then(resolve, reject);
        }));
    } finally {
        clearTimeout(timer);
        parent?.signal.removeEventListener("abort", abortParent);
        controller.abort(diagnostic(stage, "stage disposed"));
    }
}

async function waitFor(check, description = "observable condition", timeoutMs = 10000) {
    return bounded(description, timeoutMs, async signal => {
        while (!signal.aborted) {
            try { const value = await check(); if (value) return value; }
            catch (error) {
                // Connection refusal during launch is expected. Semantic/CDP failures are terminal.
                if (!["ECONNREFUSED", "ECONNRESET"].includes(error.code)) throw error;
            }
            await pause(Math.min(50, Math.max(0, stages.getStore().deadline - performance.now())));
        }
        throw signal.reason;
    });
}

function request(url, { json = false, timeoutMs = Math.max(1, (stages.getStore()?.deadline ?? (performance.now() + 10000)) - performance.now()), maxBytes = 1024 * 1024, child } = {}) {
    return bounded(json ? "HTTP JSON response" : "HTTP readiness response", timeoutMs, signal => new Promise((resolve, reject) => {
        let body = "", bytes = 0;
        const assertAlive = () => {
            if (child && (child.exitCode !== null || child.signalCode !== null)) throw diagnostic("fixture readiness", processFixture.childDiagnostic(child));
        };
        try { assertAlive(); } catch (error) { reject(error); return; }
        const req = http.get(url, { signal }, response => {
            response.setEncoding("utf8");
            response.on("data", chunk => {
                bytes += Buffer.byteLength(chunk);
                if (bytes > maxBytes) {
                    const error = diagnostic("HTTP response", "body exceeds capture bound");
                    reject(error); response.destroy(); req.destroy();
                }
                else if (json) body += chunk;
            });
            response.on("error", reject);
            response.on("end", () => {
                try {
                    assertAlive();
                    if (json && response.statusCode !== 200) throw diagnostic("HTTP JSON response", `unexpected status ${response.statusCode}`);
                    resolve(json ? JSON.parse(body) : response.statusCode);
                } catch (error) { reject(error); }
            });
        });
        req.on("error", reject);
    }));
}
const getJson = (url, options) => request(url, { ...options, json: true });
const getStatus = (url, options) => request(url, options);

function freePort() {
    return bounded("allocate fixture port", 2000, () => new Promise((resolve, reject) => {
        const server = net.createServer();
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            const port = server.address().port;
            server.close(error => error ? reject(error) : resolve(port));
        });
    }));
}

class CdpClient extends EventEmitter {
    constructor(url, { child, expectedDialogs = [], timeoutMs = 15000 } = {}) {
        super();
        this.socket = stages.exit(() => new WebSocket(url, { maxPayload: 32 * 1024 * 1024 }));
        this.child = child;
        this.timeoutMs = timeoutMs;
        this.nextId = 1;
        this.pending = new Map();
        this.dialogs = [];
        this.acceptDialogs = true;
        this.expectedDialogs = new Set(expectedDialogs);
        this.failure = null;
        this.onExit = () => this.fail(diagnostic("CDP child exit", processFixture.childDiagnostic(child)));
        child?.once("exit", this.onExit);
        this.socket.on("error", error => this.fail(diagnostic("CDP socket", error.message)));
        this.socket.on("close", () => this.fail(diagnostic("CDP disconnect", "socket closed with outstanding operation or before disposal")));
        this.socket.on("message", raw => {
            if (this.failure) return;
            try {
                const message = JSON.parse(raw);
                if (!message || typeof message !== "object" || Array.isArray(message)) throw Error("invalid CDP document");
                if (message.method === "Page.javascriptDialogOpening") {
                    this.dialogs.push(message.params?.message);
                    const expected = message.params?.type === "confirm" && this.expectedDialogs.has(message.params.message);
                    if (!expected) { this.fail(diagnostic("CDP dialog", "unexpected JavaScript dialog")); return; }
                    void this.send("Page.handleJavaScriptDialog", { accept: this.acceptDialogs }).catch(error => this.fail(error));
                    return;
                }
                if (message.id !== undefined) {
                    if (!Number.isInteger(message.id) || (!message.error && !("result" in message))) throw Error("invalid CDP response");
                    const pending = this.pending.get(message.id);
                    if (!pending) return; // A late response to an expired request cannot satisfy another request.
                    if (message.error) pending.reject(diagnostic(pending.method, message.error.message || "protocol error"));
                    else if (message.result?.exceptionDetails) pending.reject(diagnostic(pending.method, (message.result.exceptionDetails.exception?.description || message.result.exceptionDetails.text || "JavaScript evaluation exception").slice(0, 8192)));
                    else pending.resolve(message.result);
                } else if (typeof message.method !== "string") throw Error("CDP message has neither response ID nor event method");
                else this.emit("event", message);
            } catch (error) { this.fail(diagnostic("CDP framing", error.message)); }
        });
    }
    fail(error) {
        this.failure ||= error;
        this.dialogError = this.failure;
        for (const pending of [...this.pending.values()]) pending.reject(this.failure);
    }
    async open() {
        if (this.failure) throw this.failure;
        if (this.socket.readyState === WebSocket.OPEN) return;
        await bounded("CDP connection", this.timeoutMs, signal => new Promise((resolve, reject) => {
            const finish = error => {
                this.socket.off("open", ready); this.socket.off("error", failed); this.socket.off("close", closed); signal.removeEventListener("abort", aborted);
                error ? reject(error) : resolve();
            };
            const ready = () => finish();
            const failed = error => finish(error);
            const closed = () => finish(this.failure || diagnostic("CDP connection", "closed before open"));
            const aborted = () => finish(signal.reason);
            this.socket.once("open", ready); this.socket.once("error", failed); this.socket.once("close", closed); signal.addEventListener("abort", aborted, { once: true });
        }));
    }
    async send(method, params = {}, timeoutMs = this.timeoutMs) {
        if (this.failure) throw this.failure;
        if (this.socket.readyState !== WebSocket.OPEN) throw diagnostic(method, "CDP socket is not open");
        return bounded(method, timeoutMs, signal => new Promise((resolve, reject) => {
            const id = this.nextId++;
            const finish = (error, value) => {
                this.pending.delete(id); signal.removeEventListener("abort", aborted);
                error ? reject(error) : resolve(value);
            };
            const aborted = () => finish(signal.reason);
            signal.addEventListener("abort", aborted, { once: true });
            this.pending.set(id, { method, resolve: value => finish(null, value), reject: error => finish(error) });
            this.socket.send(JSON.stringify({ id, method, params }), error => { if (error) finish(error); });
        }));
    }
    async evaluate(expression) {
        const result = await this.send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
        if (!result?.result || !("value" in result.result) && result.result.type !== "undefined") throw diagnostic("Runtime.evaluate", "missing by-value result");
        const value = result.result.value;
        if (stages.getStore()) stages.getStore().trace.lastObserved = `evaluated ${typeof value}${typeof value === "boolean" ? " " + value : ""}`;
        return value;
    }
    async settleRendering() {
        // Finish only finite visual animations; persistent status animations are not readiness.
        await this.evaluate(`(async()=>{
            await document.fonts.ready;
            document.getAnimations().forEach(animation=>{
                if(Number.isFinite(animation.effect?.getComputedTiming().endTime)) animation.finish();
            });
            await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
        })()`);
    }
    eval(expression) { return this.evaluate(expression); }
    close() {
        if (this.disposed) return;
        this.disposed = true;
        this.fail(diagnostic("CDP disposal", "client disposed"));
        this.child?.off("exit", this.onExit);
        this.removeAllListeners();
        this.socket.once("close", () => this.socket.removeAllListeners());
        this.socket.terminate();
    }
}

function createBrowserProfile() {
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), "wsprrypi-browser-"));
    profiles.set(profile, { child: null });
    return profile;
}

function waitForPhp(child, url, description = "PHP fixture ready", timeoutMs = 10000) {
    return waitFor(async () => {
        if (child) {
            if (child.exitCode !== null || child.signalCode !== null) throw diagnostic("PHP fixture startup", processFixture.childDiagnostic(child));
            // PHP emits this acknowledgment only after its own bind succeeded.
            if (!/Development Server.*started/.test(processFixture.childDiagnostic(child))) return false;
        }
        return await getStatus(url, { child }) === 200;
    }, description, timeoutMs);
}

function browserBinary() {
    if (process.env.CHROME_BIN) return process.env.CHROME_BIN;
    if (process.env.CHROMIUM_BIN) return process.env.CHROMIUM_BIN;
    return process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "chromium";
}

async function launchBrowser({ executable = browserBinary(), args = [], profile, timeoutMs = 10000, expectedDialogs = [] } = {}) {
    profile ||= createBrowserProfile();
    let browser, client, profileOwned = false;
    try {
        return await bounded("browser startup and connection", timeoutMs, async () => {
        // Caller profiles must be fresh too. PID-named profiles and debug-port reservations are not reused.
        if (!fs.existsSync(profile)) fs.mkdirSync(profile, { recursive: true });
        if (fs.readdirSync(profile).length) throw diagnostic("browser launch", "profile is not empty");
        profileOwned = true;
        if (!profiles.has(profile)) profiles.set(profile, { child: null });
        browser = processFixture.spawnIsolatedProcess(executable, [
            ...args.filter(arg => arg.startsWith("--") && !arg.startsWith("--remote-debugging-") && !arg.startsWith("--user-data-dir=")),
            "--remote-debugging-port=0", `--user-data-dir=${profile}`, "about:blank"
        ]);
        profiles.get(profile).child = browser;
        await processFixture.waitForSpawn(browser);
        const page = await bounded("browser usable target ready", timeoutMs, async () => {
            return waitFor(async () => {
                if (browser.exitCode !== null || browser.signalCode !== null) throw diagnostic("browser early exit", processFixture.childDiagnostic(browser));
                const portFile = path.join(profile, "DevToolsActivePort");
                if (!fs.existsSync(portFile)) { stages.getStore().trace.lastObserved = processFixture.childDiagnostic(browser); return false; }
                const [port, browserPath] = fs.readFileSync(portFile, "utf8").split("\n");
                stages.getStore().trace.lastObserved = "DevTools discovery file observed";
                if (!/^\d+$/.test(port) || +port < 1 || +port > 65535 || !/^\/devtools\/browser\/[a-f0-9-]{36}$/.test(browserPath || "")) return false;
                const origin = `http://127.0.0.1:${port}`;
                const version = await getJson(`${origin}/json/version`, { child: browser });
                if (typeof version.Browser !== "string" || new URL(version.webSocketDebuggerUrl).pathname !== browserPath) throw diagnostic("browser endpoint identity", "discovery does not match owned profile");
                const pages = await getJson(`${origin}/json/list`, { child: browser });
                if (!Array.isArray(pages)) throw diagnostic("browser target discovery", "expected page list");
                const target = pages.find(item => item.type === "page" && typeof item.webSocketDebuggerUrl === "string");
                if (!target) { stages.getStore().trace.lastObserved = "endpoint ready without a usable page"; return false; }
                const endpoint = new URL(target.webSocketDebuggerUrl);
                if (endpoint.hostname !== "127.0.0.1" || endpoint.port !== port || !endpoint.pathname.startsWith("/devtools/page/")) throw diagnostic("browser target identity", "target is not owned loopback endpoint");
                console.log(`Browser executable=${browser.spawnfile}; version=${version.Browser}; target=${target.id}`);
                return target;
            }, "endpoint discovered; waiting for usable page", timeoutMs);
        });
        client = new CdpClient(page.webSocketDebuggerUrl, { child: browser, expectedDialogs });
        client.profile = profile;
        await client.open();
        await client.send("Page.enable");
        return { browser, client, page, profile };
        });
    } catch (error) {
        await finishFixture({ failure: error, client, browser, profile: profileOwned ? profile : undefined });
    }
}

async function closeServer(server) {
    if (!server.listening) return;
    server.closeAllConnections?.();
    await bounded("HTTP fixture cleanup", 5000, () => new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve())));
}

async function finishFixture({ failure, client, browser, php, profile, cleanups = [] } = {}) {
    const owned = profile && profiles.get(profile);
    browser ||= owned?.child;
    const errors = failure ? [failure] : [];
    const attempt = async action => { try { await action(); return true; } catch (error) { if (!errors.includes(error)) errors.push(error); return false; } };
    await attempt(() => client?.close());
    const stopped = processFixture.failedBeforeSpawn(browser) || await attempt(() => processFixture.stopIsolatedProcess(browser));
    await attempt(() => processFixture.stopChild(php));
    for (const action of cleanups) await attempt(() => bounded("fixture secondary cleanup", 5000, action));
    // Never remove a profile when writers might remain, even if another cleanup failed.
    if (stopped && owned && (!owned.child || owned.child === browser)) {
        if (await attempt(() => fs.rmSync(profile, { recursive: true, force: true, maxRetries: 3, retryDelay: 100 }))) profiles.delete(profile);
    }
    if (errors.length) {
        const error = new AggregateError(errors, "Browser workflow or cleanup failed", { cause: failure || errors[0] });
        // Preserve ownership on failure without dumping sockets or request payloads to logs.
        Object.defineProperty(error, "resources", { value: { browser, client, profile } });
        throw error;
    }
}

module.exports = { CdpClient, launchBrowser, finishFixture, browserBinary, createBrowserProfile, waitForPhp, closeServer, bounded, waitFor, getJson, getStatus, freePort, ...processFixture };
