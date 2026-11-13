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
    let timerId = 0, releaseRead, blockRead = true, now = 0;
    const root = {document: {hidden: false, readyState: "complete", getElementById: () => null, addEventListener: (name, fn) => documentEvents.set(name, fn)},
        performance: {now: () => now}, crypto: crypto.webcrypto,
        sessionStorage: {getItem: () => JSON.stringify(references), setItem: () => {}},
        addEventListener: (name, fn) => events.set(name, fn),
        fetch: async (url, options) => {
            calls.push({url, method: options.method});
            assert.equal(options.cache, "no-store");
            let value = fleet;
            if (String(url).includes("/actions?")) {
                const ids = new URL(url, "http://fixture").searchParams.get("ids").split(",");batches.push(ids);
                value = {controller_incarnation: incarnation, publication_revision: "1", actions: Object.fromEntries(ids.map(id => [id, {action_id: id, state: "unknown_or_expired"}]))};
            } else if (blockRead) {
                blockRead = false;await new Promise(resolve => {releaseRead = resolve;});
            }
            return {ok: true, status: 200, headers: {get: () => '"fleet"'}, json: async () => structuredClone(value)};
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
    await assert.rejects(root.FleetClient.mutate({operation: "enable", device_id: hex(1)}, root.FleetClient.capture(hex(1))), /review/i);
    assert.equal(calls.some(c => c.method === "POST"), false, "unconfirmed work never triggers a mutating retry");
    await assert.rejects(root.FleetClient.mutate({operation: "assign", settings: {"Device ID": hex(33)}}, {}), /tracking is full/);
    await assert.rejects(root.FleetClient.mutate({operation: "pause", device_id: hex(1)}, root.FleetClient.capture(hex(1))), /earlier stop/);
    root.FleetClient.review(hex(1));
    assert.equal(root.FleetClient.snapshot().actions.length, 62, "explicit current-state review retires only the selected references");
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
    events.get("pagehide")();assert.equal(timeouts.size, 0);assert.equal(intervals.size, 0);
    console.log("Fleet client: bounded blocked-read coalescing, 64-reference rotation, explicit retirement, freshness, hidden-page suspension and BFCache return passed.");
}
main().catch(error => {console.error(error);process.exitCode = 1;});
