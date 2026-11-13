// SPDX-License-Identifier: MIT
// Shipped Fleet browser client -> production coordinator -> selected actual Pico
// TLS/USB handler and production Pi simulated backend. No device nodes or RF.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), vm = require("node:vm"), crypto = require("node:crypto");
const {spawnFixtureChild, stopIsolatedProcess} = require("./browser_fixture_process.js"), readline = require("node:readline");
const [driver, carrier, peer, credentials] = process.argv.slice(2);
if (!credentials) throw new Error("Expected Fleet driver, network|usb, actual Pico driver, credentials directory");
async function main() {
    const child = spawnFixtureChild(driver, [carrier, peer, credentials], {detached: process.platform !== "win32", stdio: ["pipe", "pipe", "pipe"]});
    const replies = [], waiters = [];let stderr = "", failed, ready, rejectReady, readySeen = false;
    child.stderr.on("data", data => {stderr = (stderr + data).slice(-8192);});
    const readyPromise = new Promise((resolve, reject) => {ready = resolve;rejectReady = reject;});
    readline.createInterface({input: child.stdout}).on("line", line => {
        if (line === "READY") {readySeen = true;ready();return;}
        if (!line.startsWith("{")) return;
        const value = JSON.parse(line);const next = waiters.shift();if (next) next.resolve(value);else replies.push(value);
    });
    child.on("error", error => {failed = error;if (!readySeen) rejectReady(error);for (const next of waiters.splice(0)) next.reject(error);});
    child.on("exit", code => {if (code) failed = new Error(`Fleet driver exit ${code}: ${stderr}`);if (!readySeen) rejectReady(failed || new Error("Fleet driver exited before readiness"));for (const next of waiters.splice(0)) next.reject(failed || new Error("Fleet driver exited"));});
    const deadline = setTimeout(() => {failed = new Error("Fleet pair watchdog: " + stderr);for (const next of waiters.splice(0)) next.reject(failed);if (!readySeen) rejectReady(failed);void stopIsolatedProcess(child).catch(() => {});}, 120000);
    const events = new Map(), storage = new Map();let root;
    function exchange(request) {
        if (failed) return Promise.reject(failed);
        const response = replies.length ? Promise.resolve(replies.shift()) : new Promise((resolve, reject) => waiters.push({resolve, reject}));
        child.stdin.write(JSON.stringify(request) + "\n");return response;
    }
    try {
        await readyPromise;
        root = {document: {readyState: "loading", hidden: false, getElementById: () => null, addEventListener: (event, fn) => events.set("document:" + event, fn)},
            performance, crypto: crypto.webcrypto, sessionStorage: {getItem: key => storage.get(key) || null, setItem: (key, value) => storage.set(key, value)},
            addEventListener: (event, fn) => events.set(event, fn), console,
            fetch: async (url, options) => {
                const suffix = String(url).split("/host/fleet")[1], query = new URL("http://fixture/fleet" + suffix);
                const result = await exchange({method: options.method || "GET", resource: query.pathname.endsWith("/actions") ? "actions" : query.pathname.endsWith("/operations") ? "operations" : "fleet",
                    ids: query.searchParams.get("ids") || "", body: options.body || "", etag: options.headers?.["If-Match"] || ""});
                if (result.error) throw new Error(result.error);
                return {status: result.status, ok: result.status >= 200 && result.status < 300, json: async () => result.body, headers: {get: () => result.etag}};
            }};
        const context = vm.createContext({window: root, AbortSignal, Uint8Array, setTimeout, clearTimeout, setInterval, clearInterval});
        vm.runInContext(fs.readFileSync(path.join(__dirname, "../data/fleet-client.js"), "utf8"), context);
        const client = root.FleetClient;
        const wait = async (condition, reason) => {
            const limit = performance.now() + 45000;
            while (performance.now() < limit) {await client.refresh();if (condition(client.snapshot())) return;await new Promise(resolve => setTimeout(resolve, 30));}
            throw new Error(reason + ": " + JSON.stringify(client.snapshot().data));
        };
        await wait(state => Object.keys(state.data.outputs).length === 2 && Object.values(state.data.outputs).every(o => o.can_resume), "two actual outputs inspected");
        const pico = (carrier === "network" ? "a" : "d").repeat(32), pi = "b".repeat(32);
        assert.equal(client.snapshot().data.outputs[pico].identity.product, "WsprryPico");
        assert.equal(client.snapshot().data.outputs[pi].identity.product, "WsprryPi");
        const mutate = async (operation, id) => {
            for (let review = 0; review < 12; review++) {
                await client.refresh();
                try {return await client.mutate({operation, device_id: id}, client.capture(id));}
                catch (error) {if (!error.message.includes("revision_conflict")) throw error;
                    await client.refresh();client.review(id);}
            }
            throw new Error("Fixture explicit read/review conflict budget exhausted");
        };
        const enable = await mutate("enable", pico);assert.ok(["accepted", "pending", "confirmed"].includes(enable.state));
        await wait(state => state.data.assignments.find(r => r.device_id === pico).enabled, "Pico enable committed");
        assert.equal(client.snapshot().data.assignments.find(r => r.device_id === pi).enabled, false, "selected Pico cannot enable the Pi");
        const pause = await mutate("pause", pico);
        await wait(() => client.outcome(pico)?.action_id === pause.action_id && client.outcome(pico)?.state === "confirmed", "Pico pause confirmed through owning coordinator");
        await mutate("enable", pi);
        await wait(state => state.data.assignments.find(r => r.device_id === pi).enabled, "simulated Pi enable committed");
        assert.equal(client.snapshot().data.assignments.find(r => r.device_id === pico).enabled, false, "selected Pi cannot enable Pico");
        const stopPi = await mutate("pause", pi);
        await wait(() => client.outcome(pi)?.action_id === stopPi.action_id && client.outcome(pi)?.state === "confirmed", "simulated Pi pause confirmed");
        assert.equal(client.snapshot().data.assignments.some(r => r.enabled), false);
        const old = client.capture(pi), oldIncarnation = client.snapshot().data.controller_incarnation;
        await exchange({replace: true});
        await wait(state => state.data.controller_incarnation !== oldIncarnation && Object.values(state.data.outputs).every(o => o.can_resume), "manager replacement retires old sessions/identity leases");
        await assert.rejects(client.mutate({operation: "enable", device_id: pi}, old), /changed after review/);
        const readStart = performance.now();await client.refresh();assert.ok(performance.now() - readStart < 1000, "published read remains bounded");
        console.log(`Fleet Operations ${carrier}: shipped browser correlation/selection, actual Pico handlers, independent simulated Pi, committed enable, priority pause and owned cleanup passed.`);
    } finally {
        events.get("pagehide")?.();clearTimeout(deadline);child.stdin.end();
        if (child.exitCode === null && child.signalCode === null) {
            let grace;
            await Promise.race([new Promise(resolve => child.once("exit", resolve)), new Promise(resolve => {grace = setTimeout(resolve, 1000);})]);clearTimeout(grace);
        }
        await stopIsolatedProcess(child);
    }
}
main().catch(error => {console.error(error);process.exitCode = 1;});
