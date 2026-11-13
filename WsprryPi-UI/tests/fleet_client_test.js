// SPDX-License-Identifier: MIT
// Deterministic browser lifecycle/queue bounds; production carrier evidence is separate.
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), vm = require("node:vm"), crypto = require("node:crypto");
async function main() {
    const incarnation = "f".repeat(32), boot = "e".repeat(32), hex = n => n.toString(16).padStart(32, "0");
    const outputs = {}, assignments = [], references = [];
    for (let n = 1; n <= 32; n++) {
        const id = hex(n);
        assignments.push({device_id: id, name: "Output " + n, schedule: {}, enabled: false, in_flight: false});
        outputs[id] = {assignment_generation: id, control_revision: "0", observation_revision: "1", observation_age_ms: "0", boot_id: boot, can_resume: true, state: "idle", actions: []};
        for (const priority of [false, true]) references.push({action_id: hex(100 + n + (priority ? 32 : 0)), device_id: id, assignment_generation: id,
            controller_incarnation: incarnation, reviewed_boot_id: boot, operation: priority ? "pause" : "enable", priority, state: "outcome_unconfirmed"});
    }
    const fleet = {scope: "wsprrypi-wtp-fleet/1", maximum_outputs: 32, controller_incarnation: incarnation, publication_revision: "1", now_ms: "1000", assignments, outputs};
    const timeouts = new Map(), intervals = new Map(), events = new Map(), documentEvents = new Map(), calls = [], batches = [];
    let timerId = 0, releaseRead, releasePost, blockRead = true, now = 0, postStatus = 200;
    const root = {document: {hidden: false, readyState: "complete", getElementById: () => null, addEventListener: (name, fn) => documentEvents.set(name, fn)},
        performance: {now: () => now}, crypto: crypto.webcrypto,
        sessionStorage: {getItem: () => JSON.stringify(references), setItem: () => {}},
        addEventListener: (name, fn) => events.set(name, fn),
        fetch: async (url, options) => {
            calls.push({url, method: options.method});
            assert.equal(options.cache, "no-store");
            let value = fleet;
            if (options.method === "POST") {
                const command = JSON.parse(options.body);
                const output = outputs[command.device_id];output.control_revision = String(BigInt(output.control_revision) + 1n);
                value = {action: {...command, control_revision: output.control_revision, state: "pending", work_pending: true}};
                output.actions = [value.action];
                await new Promise(resolve => {releasePost = resolve;});
            } else if (String(url).includes("/actions?")) {
                const ids = new URL(url, "http://fixture").searchParams.get("ids").split(",");batches.push(ids);
                value = {controller_incarnation: incarnation, publication_revision: "1", actions: Object.fromEntries(ids.map(id => [id, {action_id: id, state: "unknown_or_expired"}]))};
            } else if (blockRead) {
                blockRead = false;await new Promise(resolve => {releaseRead = resolve;});
            }
            const status = options.method === "POST" ? postStatus : 200;
            return {ok: status === 200, status, headers: {get: () => '"fleet"'}, json: async () => structuredClone(value)};
        }};
    const context = vm.createContext({window: root, AbortSignal, Uint8Array,
        setTimeout: (fn, delay) => {const id = ++timerId;timeouts.set(id, {fn, delay});return id;}, clearTimeout: id => timeouts.delete(id),
        setInterval: fn => {const id = ++timerId;intervals.set(id, fn);return id;}, clearInterval: id => intervals.delete(id)});
    const settle = async () => {for (let n = 0; n < 15; n++) await new Promise(resolve => setImmediate(resolve));};
    const tick = async () => {assert.equal(timeouts.size, 1, "one bounded background timer");const [id, timer] = [...timeouts][0];timeouts.delete(id);timer.fn();await settle();};
    vm.runInContext(fs.readFileSync(path.join(__dirname, "../data/fleet-client.js"), "utf8"), context);
    await settle();assert.equal(calls.length, 1);
    for (let n = 0; n < 40; n++) documentEvents.get("visibilitychange")();
    assert.equal(calls.length, 1, "concurrent resume notifications cannot multiply a blocked read");
    releaseRead();await settle();await tick();
    assert.equal(root.FleetClient.snapshot().valid, true);
    assert.equal(batches[0].length, 32);await tick();
    assert.equal(batches[1].length, 32);
    assert.equal(new Set([...batches[0], ...batches[1]]).size, 64, "lookup rotates across all retained references");
    fleet.now_ms = 1000;
    await assert.rejects(root.FleetClient.refresh(), /Invalid Fleet/, "64-bit host metadata must be decimal strings, never coerced numbers");
    fleet.now_ms = "1000";await root.FleetClient.refresh();
    for (const invalid of ["18446744073709551616", "01", ["1000"], null]) {
        fleet.now_ms = invalid;await assert.rejects(root.FleetClient.refresh(), /Invalid Fleet/);
    }
    fleet.now_ms = "1000";fleet.controller_incarnation = [incarnation];
    await assert.rejects(root.FleetClient.refresh(), /Invalid Fleet/, "an array cannot masquerade as an incarnation");
    fleet.controller_incarnation = incarnation;await root.FleetClient.refresh();
    await assert.rejects(root.FleetClient.mutate({operation: "enable", device_id: hex(1)}, root.FleetClient.capture(hex(1))), /review/i);
    assert.equal(calls.some(c => c.method === "POST"), false, "unconfirmed work never triggers a mutating retry");
    await assert.rejects(root.FleetClient.mutate({operation: "assign", settings: {"Device ID": hex(33)}}, {}), /tracking is full/);
    await assert.rejects(root.FleetClient.mutate({operation: "pause", device_id: hex(1)}, root.FleetClient.capture(hex(1))), /earlier stop/);
    root.FleetClient.review(hex(1));
    assert.equal(root.FleetClient.snapshot().actions.length, 62, "explicit current-state review retires only the selected references");
    const pending = root.FleetClient.mutate({operation: "pause", device_id: hex(1)}, root.FleetClient.capture(hex(1)));
    await settle();assert.ok(releasePost, "POST is held after reference creation");
    assert.throws(() => root.FleetClient.review(hex(1)), /current request/, "review cannot discard an in-flight POST reference");
    assert.equal(root.FleetClient.snapshot().actions.filter(r => r.device_id === hex(1)).length, 1);
    blockRead = true;releasePost();await pending;await settle();
    assert.equal(root.FleetClient.snapshot().valid, false, "pre-POST state cannot authorize immediate reference retirement");
    assert.throws(() => root.FleetClient.review(hex(1)), /Refresh/);
    releaseRead();await settle();
    assert.throws(() => root.FleetClient.review(hex(1)), /Current work/, "fresh active controller callbacks remain protected");
    outputs[hex(1)].actions = [];await root.FleetClient.refresh();
    assert.doesNotThrow(() => root.FleetClient.review(hex(1)), "a fresh quiescent owner read supersedes a stale browser work-pending flag");
    outputs[hex(2)].state = "recovery_required";assignments[1].in_flight = true;
    await root.FleetClient.refresh();
    assert.doesNotThrow(() => root.FleetClient.review(hex(2)), "terminal unknown references can release capacity without clearing device recovery duty");
    assert.equal(root.FleetClient.snapshot().actions.filter(r => r.device_id === hex(2)).length, 0);
    assert.equal(root.FleetClient.snapshot().data.assignments[1].in_flight, true);
    root.FleetClient.review(hex(4));postStatus = 500;
    const failed = root.FleetClient.mutate({operation: "enable", device_id: hex(4)}, root.FleetClient.capture(hex(4)));
    const failedCheck = assert.rejects(failed, /500/);await settle();releasePost();await failedCheck;await settle();
    assert.equal(root.FleetClient.snapshot().actions.filter(r => r.device_id === hex(4)).length, 1, "HTTP 500 retains action correlation because admission is uncertain");
    root.FleetClient.review(hex(5));postStatus = 409;
    const refused = root.FleetClient.mutate({operation: "enable", device_id: hex(5)}, root.FleetClient.capture(hex(5)));
    const refusedCheck = assert.rejects(refused, /409/);await settle();releasePost();await refusedCheck;await settle();
    assert.equal(root.FleetClient.snapshot().actions.filter(r => r.device_id === hex(5)).length, 0);
    assert.equal(root.FleetClient.outcome(hex(5)).state, "refused", "definite refusal remains visible instead of an invented unknown result");
    root.FleetClient.review(hex(6));postStatus = 500;
    const lateFailure = root.FleetClient.mutate({operation: "enable", device_id: hex(6)}, root.FleetClient.capture(hex(6)));
    await settle();outputs[hex(6)].actions[0].state = "confirmed";outputs[hex(6)].actions[0].work_pending = false;
    await root.FleetClient.refresh();assert.equal(root.FleetClient.outcome(hex(6)).state, "confirmed");
    releasePost();assert.equal((await lateFailure).state, "confirmed", "a delayed HTTP error cannot overwrite already verified completion");
    await settle();assert.equal(root.FleetClient.outcome(hex(6)).state, "confirmed");
    now = 14999;assert.equal(root.FleetClient.age(outputs[hex(1)]), 14999n);
    now = 15000;assert.equal(root.FleetClient.age(outputs[hex(1)]), 15000n);
    const beforeHide = calls.length;
    root.document.hidden = true;documentEvents.get("visibilitychange")();await settle();
    assert.equal(timeouts.size, 0);await assert.rejects(root.FleetClient.refresh(), /suspended/);
    assert.equal(calls.length, beforeHide, "hidden pages do not issue queued reads");
    events.get("pagehide")();assert.equal(intervals.size, 0);
    root.document.hidden = false;events.get("pageshow")({persisted: true});await settle();
    assert.equal(intervals.size, 1, "BFCache return restores local freshness rendering once");
    assert.equal(root.FleetClient.snapshot().valid, true);assert.equal(timeouts.size, 1);
    fleet.controller_incarnation = "c".repeat(32);
    for (const output of Object.values(outputs)) output.actions = [];
    outputs[hex(3)].state = "recovery_required";
    assignments[2].in_flight = true;
    await root.FleetClient.refresh();
    assert.doesNotThrow(() => root.FleetClient.review(hex(3)), "obsolete controller references can be explicitly retired while the new owner retains its recovery barrier");
    assert.equal(root.FleetClient.snapshot().actions.filter(r => r.device_id === hex(3)).length, 0);
    assert.equal(root.FleetClient.snapshot().data.outputs[hex(3)].state, "recovery_required", "reference retirement never clears controller recovery duty");
    events.get("pagehide")();assert.equal(timeouts.size, 0);assert.equal(intervals.size, 0);
    console.log("Fleet client: bounded blocked-read coalescing, 64-reference rotation, explicit retirement, freshness, hidden-page suspension and BFCache return passed.");
}
main().catch(error => {console.error(error);process.exitCode = 1;});
