// SPDX-License-Identifier: MIT
"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), vm = require("node:vm"), path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "../data/wtp.js"), "utf8");
const { defaults, errors, summarize, validNetworkIdentity } = require("../data/wtp.js");
const settings = { ...defaults, Endpoint: "/dev/serial/by-id/pico-if02", "USB Serial": "000012345678", "Device ID": "4".repeat(32), "USB Vendor ID": 51966, "USB Product ID": 16402 };
assert.deepEqual(errors(settings, true), {});
assert.deepEqual(errors({...settings,"Device ID":""},true),{});
assert.match(summarize(null).text, /unavailable/);
assert.match(summarize({selected:false}).text, /not selected/);
assert.deepEqual(errors(defaults, false), {});
assert.equal(Object.keys(errors(defaults, true)).length, 4);
for (const bad of [-1, 1.2, NaN, 65536, true]) assert(errors({ ...settings, "USB Vendor ID": bad }, true)["USB Vendor ID"]);
for (const value of ["PiCo.LoCaL.", "pico.local", "localhost", "127.0.0.1", "::1", "2001:0DB8::1", "::ffff:192.0.2.1", "a".repeat(63) + ".local"]) assert(validNetworkIdentity(value), value);
for (const value of ["", "pico.local..", "pico.local\n", "127.0.0.1\n", "pico.local\u2028", "pico..local", "-pico.local", "pico-.local", "pico_local", "pico.local:443", "*.local", "pico.local/", "127.1", "0127.0.0.1", "0x7f000001", "0x7f000001.", "256.0.0.1", "127.0.0.1.", "[::1]", "::g", "1:::2", "a".repeat(64) + ".local"]) assert(!validNetworkIdentity(value), value);
const named = { ...settings, Transport: "network", Hostname: "PiCo.LoCaL.", "TLS Server Identity": "PICO.LOCAL.", "TCP Port": 18443, "TLS CA File": "/ca.crt", "TLS Client Certificate": "/client.crt", "TLS Client Key": "/client.key" };
assert.deepEqual(errors(named, true), {});
const plain = { ...settings, Transport: "network_plain", Hostname: "pico.local", "TCP Port": 31417, "Device ID": "" };
assert.deepEqual(errors(plain, true), {});
assert.deepEqual(errors({ ...plain, "TLS Server Identity": "stale:invalid" }, true), {});
assert(errors({ ...plain, "TCP Port": 0 }, true)["TCP Port"]);
assert.equal(defaults["TCP Port"], 31417);
assert.equal(defaults["Start Uncertainty ns"], 100000000);
assert.equal(named.Hostname, "PiCo.LoCaL.");
assert(errors({ ...named, "TLS Server Identity": "pico.local:443" }, true)["TLS Server Identity"]);
assert(errors({ ...named, Transport: "usb", Hostname: "pico..local" }, false).Hostname);
const status = { selected: true, ready: true, phase: "idle", session_phase: "ready", now_ms: "9007199254749999", status_observed_ms: "9007199254740999", remote: { output_active: false }, identity: { device_id: "d", boot_id: "b" }, last_report: { outcome: "complete", job_id: "old" } };
assert.match(summarize(status).output, /Inactive \(9 s ago/);
assert.equal(summarize({ ...status, remote: null }).output, "Unknown");
assert.match(summarize({ ...status, recovery_required: true }).text, /unresolved/);
const bindingMismatch=summarize({...status,usb:{binding_fault:true}});
assert.match(bindingMismatch.text,/USB device identity changed.*Reconcile/);
assert.doesNotMatch(bindingMismatch.text,/safety remains unresolved/);
assert.equal(bindingMismatch.recover,true);
assert.match(bindingMismatch.output,/Inactive/);
assert.equal(summarize({ ...status, worker_active: true }).recover, false);
assert.equal(summarize({ ...status, session_phase: "identity_changed" }).recover, false);
assert.match(summarize({ ...status, remote: { output_active: true } }).output, /^Active/); // historical completion cannot override current output
const markup = fs.readFileSync(path.join(__dirname, "../data/views/wtp-controls.php"), "utf8");
assert.match(markup, /value="network_plain">Plain LAN/);
assert.match(markup, /data-wtp-security="tls"/);
assert.doesNotMatch(markup, /wtp_visible|Show Pico development controls|wtp-visibility-hint/);
function browser(storage = new Map(), storageBlocked = false) {
    const fields = new Map();
    function field(id, key, type = "text") {
        const f = { id, dataset: key ? { wtpKey: key } : {}, type, value: "", checked: false, disabled: false, hidden: false, listeners: {}, textContent: "", setCustomValidity(v) { this.validityMessage = v; }, setAttribute() {}, addEventListener(name, fn) { this.listeners[name] = fn; } };
        fields.set(id, f); return f;
    }
    for (const id of ["wtp_use", "wtp-controls", "fleet-tab", "fleet-pane", "wtp-status-text", "wtp-output", "wtp-clock", "wtp-identity", "wtp-history", "wtp-recover", "wtp-feedback"]) field(id);
    fields.get("fleet-tab").parentElement = { hidden: false };
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
    const storage = new Map([["wsprrypi.pico-development-controls", "false"]]);
    const { root, fields, calls, writes, events, timers } = browser(storage);
    assert.equal(calls.length, 0);
    events.DOMContentLoaded();
    await new Promise(setImmediate);
    assert.equal(Object.hasOwn(root.WtpUi, "developmentControlsVisible"), false);
    assert.equal(fields.get("wtp-controls").hidden, false);
    assert.equal(fields.get("fleet-tab").parentElement.hidden, false);
    assert.equal(fields.get("fleet-tab").disabled, false);
    assert.equal(fields.get("fleet-pane").hidden, false);
    assert.equal(fields.get("wtp_use").disabled, false);
    assert.equal(root.WtpUi.selected(), false); // Visibility never selects a backend.
    assert(calls.length > 0 && calls.every(c => c.options.method === "GET"));
    assert.equal(writes.length, 0);
    assert([...fields.values()].filter(f => f.dataset.wtpKey).every(f => f.disabled));
    root.WtpUi.populate(settings); root.WtpUi.select(true);
    await new Promise(setImmediate);
    assert.equal(root.WtpUi.selected(), true);
    assert.equal(JSON.stringify(root.WtpUi.read()), JSON.stringify(settings));
    for (const saved of [1000000, 37000000, 250000000]) {
        root.WtpUi.populate({...settings, "Start Uncertainty ns": saved, "Allow Frequency Adjustment": true});
        assert.equal(root.WtpUi.read()["Start Uncertainty ns"], saved);
    }
    root.WtpUi.populate({...settings, "Start Uncertainty ns": 1000000});
    fields.get("Start Uncertainty ns").value = "100000000";
    assert.equal(root.WtpUi.read()["Start Uncertainty ns"], 100000000);
    fields.get("USB Serial").value = "";
    assert.equal(root.WtpUi.validate(), false);
    root.WtpUi.refreshControls();
    assert.equal(root.WtpUi.read()["USB Serial"], "");
    await new Promise(setImmediate);
    assert([...timers.values()].some(t => t.ms === 3000));
    root.WtpUi.select(false);
    await new Promise(setImmediate);
    assert.equal(fields.get("wtp-controls").hidden, false);
    assert.equal(root.WtpUi.read()["USB Serial"], "");
    assert([...fields.values()].filter(f => f.dataset.wtpKey).every(f => f.disabled));
    assert(calls.every(c => c.options.method === "GET"));
    const interrupted = browser();
    interrupted.events.DOMContentLoaded();
    interrupted.root.WtpUi.populate(settings); interrupted.root.WtpUi.select(true);
    await new Promise(setImmediate);
    const firmwareAvailability = [];
    interrupted.root.WtpFirmware = {setAvailability:(...args)=>firmwareAvailability.push(args)};
    interrupted.root.fetch = async()=>{throw Error("Temporary host status interruption");};
    await [...interrupted.timers.values()].find(timer=>timer.ms===3000).fn();
    assert.equal(firmwareAvailability.at(-1)[0],true,"a host status failure must not hide or cancel USB firmware monitoring");
    assert.equal(firmwareAvailability.at(-1)[3],settings["USB Serial"]);
    for (const legacyPreference of ["true", "false"]) {
        storage.set("wsprrypi.pico-development-controls", legacyPreference);
        const reloaded = browser(storage);
        reloaded.events.DOMContentLoaded();
        assert.equal(reloaded.fields.get("wtp-controls").hidden, false);
        assert.equal(reloaded.fields.get("wtp_use").disabled, false);
        assert.equal(reloaded.root.WtpUi.selected(), false);
        assert.deepEqual(reloaded.writes, []);
    }
    const blocked = browser(new Map(), true);
    blocked.events.DOMContentLoaded();
    assert.equal(blocked.fields.get("wtp-controls").hidden, false);
    assert.equal(blocked.fields.get("wtp_use").disabled, false);
    const configMarkup = fs.readFileSync(path.join(__dirname, "../data/views/config.php"), "utf8");
    assert.doesNotMatch(configMarkup, /id="fleet-tab-item"[^>]*hidden/);
    assert.doesNotMatch(configMarkup, /id="fleet-tab"[^>]*disabled/s);
    assert.doesNotMatch(configMarkup, /wtp-show-controls|wtp-hidden-selection/);
    assert.match(markup, /id="wtp-controls"[^>]*aria-labelledby="wtp-heading">/);
    console.log("Fleet is available on fresh load without console/storage flags; selection, validation, drafts and read-only polling preserved");
})().catch(error => { console.error(error); process.exitCode = 1; });
