// SPDX-License-Identifier: MIT
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), vm = require("node:vm"), path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "../data/wtp.js"), "utf8");
const { defaults, errors, summarize, validNetworkIdentity } = require("../data/wtp.js");
const settings = { ...defaults, Endpoint: "/dev/serial/by-id/pico-if02", "USB Serial": "000012345678", "Device ID": "4".repeat(32), "USB Vendor ID": 51966, "USB Product ID": 16402 };
assert.deepEqual(errors(settings, true), {});
assert.match(summarize(null).text, /unavailable/);
assert.match(summarize({selected:false}).text, /not selected/);
assert.deepEqual(errors(defaults, false), {});
assert.equal(Object.keys(errors(defaults, true)).length, 5);
for (const bad of [-1, 1.2, NaN, 65536, true]) assert(errors({ ...settings, "USB Vendor ID": bad }, true)["USB Vendor ID"]);
for (const value of ["PiCo.LoCaL.", "pico.local", "localhost", "127.0.0.1", "::1", "2001:0DB8::1", "::ffff:192.0.2.1", "a".repeat(63) + ".local"]) assert(validNetworkIdentity(value), value);
for (const value of ["", "pico.local..", "pico.local\n", "127.0.0.1\n", "pico.local\u2028", "pico..local", "-pico.local", "pico-.local", "pico_local", "pico.local:443", "*.local", "pico.local/", "127.1", "0127.0.0.1", "0x7f000001", "0x7f000001.", "256.0.0.1", "127.0.0.1.", "[::1]", "::g", "1:::2", "a".repeat(64) + ".local"]) assert(!validNetworkIdentity(value), value);
const named = { ...settings, Transport: "network", Hostname: "PiCo.LoCaL.", "TLS Server Identity": "PICO.LOCAL.", "TCP Port": 18443, "TLS CA File": "/ca.crt", "TLS Client Certificate": "/client.crt", "TLS Client Key": "/client.key" };
assert.deepEqual(errors(named, true), {});
assert.equal(named.Hostname, "PiCo.LoCaL.");
assert(errors({ ...named, "TLS Server Identity": "pico.local:443" }, true)["TLS Server Identity"]);
assert(errors({ ...named, Transport: "usb", Hostname: "pico..local" }, false).Hostname);
const status = { selected: true, ready: true, phase: "idle", session_phase: "ready", now_ms: "9007199254749999", status_observed_ms: "9007199254740999", remote: { output_active: false }, identity: { device_id: "d", boot_id: "b" }, last_report: { outcome: "complete", job_id: "old" } };
assert.match(summarize(status).output, /Inactive \(9 s ago/);
assert.equal(summarize({ ...status, remote: null }).output, "Unknown");
assert.match(summarize({ ...status, recovery_required: true }).text, /unresolved/);
assert.equal(summarize({ ...status, worker_active: true }).recover, false);
assert.equal(summarize({ ...status, session_phase: "identity_changed" }).recover, false);
assert.match(summarize({ ...status, remote: { output_active: true } }).output, /^Active/); // historical completion cannot override current output
const markup = fs.readFileSync(path.join(__dirname, "../data/views/wtp-controls.php"), "utf8");
assert.doesNotMatch(markup, /wtp_visible|Show Pico development controls|wtp-visibility-hint/);
function browser(storage = new Map(), storageBlocked = false) {
    const fields = new Map();
    function field(id, key, type = "text") {
        const f = { id, dataset: key ? { wtpKey: key } : {}, type, value: "", checked: false, disabled: false, hidden: false, listeners: {}, textContent: "", setCustomValidity(v) { this.validityMessage = v; }, setAttribute() {}, addEventListener(name, fn) { this.listeners[name] = fn; } };
        fields.set(id, f); return f;
    }
    for (const id of ["wtp-development", "wtp_use", "wtp-controls", "wtp-hidden-selection", "wtp-status-text", "wtp-output", "wtp-clock", "wtp-identity", "wtp-history", "wtp-recover", "wtp-feedback"]) field(id);
    for (const [key, value] of Object.entries(defaults)) field(key, key, typeof value === "number" ? "number" : typeof value === "boolean" ? "checkbox" : "text");
    const writes = [], calls = [], events = {}, timers = new Map();
    let nextTimer = 0;
    let response = status;
    const root = { document: { readyState: "loading", getElementById: id => fields.get(id), querySelectorAll: () => [...fields.values()].filter(f => f.dataset.wtpKey), addEventListener: (name, fn) => { events[name] = fn; } }, localStorage: { getItem: k => { if (storageBlocked) throw new Error("Storage blocked"); return storage.get(k); }, setItem: (k, v) => { if (storageBlocked) throw new Error("Storage blocked"); storage.set(k, v); writes.push(v); } }, addEventListener() {}, fetch: async (url, options) => { calls.push({ url, options }); return { ok: true, json: async () => response }; } };
    const context = { window: root, AbortController, setTimeout: (fn, ms) => { timers.set(++nextTimer, { fn, ms }); return nextTimer; }, clearTimeout: id => timers.delete(id), console };
    vm.runInNewContext(source, context);
    return { root, fields, calls, writes, events, timers };
}
(async () => {
    const storage = new Map([["wsprrypi.pico-development-controls", "true"]]);
    const { root, fields, calls, writes, events, timers } = browser(storage);
    events.DOMContentLoaded();
    assert.equal(root.WtpUi.developmentControlsVisible, false);
    assert.equal(fields.get("wtp-development").hidden, true);
    assert.equal(calls.length, 0);
    for (const invalid of ["false", "true", 0, 1, null, undefined, {}, []]) {
        assert.throws(() => { root.WtpUi.developmentControlsVisible = invalid; }, /must be a boolean/);
        assert.equal(root.WtpUi.developmentControlsVisible, false);
    }
    assert.equal(writes.length, 0);
    assert.equal(calls.length, 0);
    assert.equal(fields.get("wtp-controls").hidden, true);
    root.WtpUi.populate(settings); root.WtpUi.select(true);
    await new Promise(setImmediate);
    assert.equal(root.WtpUi.selected(), true);
    assert.equal(fields.get("wtp-hidden-selection").hidden, false);
    for (const visible of [true, false, true]) {
        root.WtpUi.developmentControlsVisible = visible;
        assert.equal(root.WtpUi.developmentControlsVisible, visible);
        assert.equal(fields.get("wtp-controls").hidden, !visible);
        assert.equal(fields.get("wtp_use").disabled, !visible);
        assert.equal(fields.get("wtp-hidden-selection").hidden, visible);
        await new Promise(setImmediate);
        assert.equal(root.WtpUi.selected(), true);
        assert.equal(JSON.stringify(root.WtpUi.read()), JSON.stringify(settings));
    }
    assert(calls.every(c => c.options.method === "GET"));
    assert.deepEqual(writes, []);
    fields.get("USB Serial").value = "";
    assert.equal(root.WtpUi.validate(), false);
    root.WtpUi.developmentControlsVisible = false;
    assert.equal(root.WtpUi.read()["USB Serial"], ""); // hiding does not restore or normalize a failed draft
    await new Promise(setImmediate);
    assert([...timers.values()].some(t => t.ms === 3000)); // Selected Pico remains monitored while hidden.
    root.WtpUi.select(false);
    assert.equal(fields.get("wtp-development").hidden, true);
    root.WtpUi.developmentControlsVisible = true;
    await new Promise(setImmediate);
    root.WtpUi.developmentControlsVisible = false;
    assert.equal(timers.size, 0); // Hidden, unselected Pico does not keep polling.
    assert(calls.every(c => c.options.method === "GET"));
    for (const value of [true, false]) {
        root.WtpUi.developmentControlsVisible = value;
        const reloaded = browser(storage);
        reloaded.events.DOMContentLoaded();
        assert.equal(reloaded.root.WtpUi.developmentControlsVisible, false);
        assert.equal(reloaded.fields.get("wtp-controls").hidden, true);
        assert.equal(reloaded.fields.get("wtp_use").disabled, true);
    }
    const blocked = browser(new Map(), true);
    blocked.events.DOMContentLoaded();
    for (const value of [true, false]) {
        blocked.root.WtpUi.developmentControlsVisible = value;
        assert.equal(blocked.fields.get("wtp-controls").hidden, !value);
    }
    const early = browser();
    early.root.WtpUi.developmentControlsVisible = true;
    assert.equal(early.calls.length, 0);
    early.events.DOMContentLoaded();
    assert.equal(early.fields.get("wtp-controls").hidden, false);
    console.log("WTP UI console boolean, reload reset, legacy preference isolation, polling, validation and draft-preservation tests passed");
})().catch(error => { console.error(error); process.exitCode = 1; });
