// SPDX-License-Identifier: MIT
// Shipped Fleet browser client -> production coordinator -> selected actual Pico
// TLS/USB handler. No execution backend, device nodes, or RF.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), vm = require("node:vm"), crypto = require("node:crypto");
const {spawnFixtureChild, stopIsolatedProcess} = require("./browser_fixture_process.js"), readline = require("node:readline");
const [driver, ...peerArguments] = process.argv.slice(2);
const [carrier, peer, credentials] = peerArguments;
if (!driver || !peer || !((carrier === "usb" && [2, 3].includes(peerArguments.length))
        || (carrier === "network" && credentials && peerArguments.length === 3))) {
    throw new Error("Expected Fleet driver, network|usb, actual Pico driver, and credentials for network");
}
async function main() {
    const child = spawnFixtureChild(driver, peerArguments, {detached: process.platform !== "win32", stdio: ["pipe", "pipe", "pipe"]});
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
        let ambiguousPost = false, ambiguousId = "";const posted = [];
        root = {document: {readyState: "loading", hidden: false, getElementById: () => null, addEventListener: (event, fn) => events.set("document:" + event, fn)},
            performance, crypto: crypto.webcrypto, sessionStorage: {getItem: key => storage.get(key) || null, setItem: (key, value) => storage.set(key, value)},
            addEventListener: (event, fn) => events.set(event, fn), console,
            fetch: async (url, options) => {
                const suffix = String(url).split("/host/fleet")[1], query = new URL("http://fixture/fleet" + suffix);
                const result = await exchange({method: options.method || "GET", resource: query.pathname.endsWith("/actions") ? "actions" : query.pathname.endsWith("/operations") ? "operations" : "fleet",
                    ids: query.searchParams.get("ids") || "", body: options.body || "", etag: options.headers?.["If-Match"] || ""});
                if (result.error) throw new Error(result.error);
                if (options.method === "POST") {
                    const command = JSON.parse(options.body);posted.push(command);
                    if (ambiguousPost && result.status === 202) {
                        ambiguousPost = false;ambiguousId = command.action_id;
                        // The real owner accepted it; lose only the host reply.
                        return {status: 500, ok: false, json: async () => ({error: {message: "Ambiguous accepted response"}}), headers: {get: () => null}};
                    }
                }
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
        await wait(state=>Object.keys(state.data.outputs).length===2,'two retained legacy outputs');
        const before=client.snapshot().data;
        assert.equal(before.assignments[0].name,before.assignments[1].name,'duplicate display names retain distinct IDs');
        for(const output of Object.values(before.outputs)) {
            assert.equal(output.state,'migration_required');
            assert.equal(output.can_resume,false);assert.equal(output.can_pause,false);assert.equal(output.can_reconcile,false);
        }
        for(const operation of ['assign','enable','pause','schedule','recover','remove']) {
            const result=await exchange({method:'POST',body:JSON.stringify({operation,device_id:'a'.repeat(32)}),etag:client.snapshot().etag,resource:'fleet'});
            assert.equal(result.status,409);assert.equal(result.body.error.code,'migration_required');
        }
        await client.refresh();assert.deepEqual(client.snapshot().data.assignments,before.assignments);
        const peerStatus=await exchange({peer_status:true});assert.equal(peerStatus.status,200);
        assert.equal(peerStatus.body.job.owner_id,null);assert.equal(peerStatus.body.job.output_active,false);
        assert.equal(peerStatus.legacy_contacts,0);assert.equal(peerStatus.saved_unchanged,true);
        const incarnation=client.snapshot().data.controller_incarnation;
        const failedReplacement=await exchange({fail_replace:true});assert.equal(failedReplacement.status,503);
        assert.equal(failedReplacement.body.error.code,'fleet_unavailable');
        await assert.rejects(client.refresh());assert.equal(client.snapshot().valid,false);
        await exchange({replace:true});
        await wait(state=>state.data.controller_incarnation!==incarnation,'fresh coordinator incarnation');
        assert.deepEqual(client.snapshot().data.assignments,before.assignments);
        const final=await exchange({peer_status:true});assert.equal(final.legacy_contacts,0);assert.equal(final.saved_unchanged,true);
        assert.equal(final.body.job.owner_id,null);assert.equal(final.body.job.output_active,false);
        console.log(`Fleet ${carrier}: shipped read coordinator, duplicate names, preserved legacy duties, refused actions, no carrier reconnect, corrupt/replaced owner, and actual Pico management status passed.`);
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
