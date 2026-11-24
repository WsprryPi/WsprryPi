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
const windows = {...named, "TLS CA File": String.raw`C:\ProgramData\WsprryPi\ca.crt`,
    "TLS Client Certificate": "D:/private/Ω/client.crt", "TLS Client Key": String.raw`C:\private\client.key`};
assert.deepEqual(errors(windows,true),{});
assert.deepEqual(errors({...settings,Endpoint:String.raw`\\?\usb#opaque-interface`},true),{});
for(const value of [String.raw`C:relative.key`,String.raw`C:\a\..\client.key`,String.raw`C:\a\key:stream`,
    String.raw`\\server\share\client.key`,String.raw`\\?\C:\client.key`,String.raw`C:\a\NUL`,String.raw`C:\a\CON .key`,
    String.raw`C:\a\COM1.key`,"C:/a/client.key ","C:/a/client.key."])
    assert(errors({...windows,"TLS Client Key":value},true)["TLS Client Key"],value);
for(const value of [undefined,17,"COM1",String.raw`\\.\COM1`,String.raw`\\?\C:\private.key`,String.raw`\\?\GLOBALROOT\Device\other`])
    assert(errors({...settings,Endpoint:value},true).Endpoint,String(value));
assert(errors({...windows,"TLS Client Key":"C:/"+"Ω".repeat(255)},true)["TLS Client Key"],"UTF-8 byte bound matches the server");
assert(errors({...windows,"TLS Client Key":"C:/private/\ud800.key"},true)["TLS Client Key"],"Invalid Unicode is rejected without changing the draft");
const plain = { ...settings, Transport: "network_plain", Hostname: "pico.local", "TCP Port": 31417, "Device ID": "" };
assert(errors(plain,true)["Device ID"]);
assert.deepEqual(errors({...plain,"Device ID":"a".repeat(32),"TLS Server Identity":"stale:invalid"},true),{});
assert(errors({ ...plain, "TCP Port": 0 }, true)["TCP Port"]);
assert.equal(defaults["TCP Port"], 80);
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
assert.match(markup, /value="network_plain">Consumer Wi-Fi/);
assert.match(markup,/Engineering HTTPS/);
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
(async()=>{
    for(const preference of ['true','false']) {
        const storage=new Map([['wsprrypi.pico-development-controls',preference]]);
        const {root,calls,writes,events,timers}=browser(storage);
        events.DOMContentLoaded();await new Promise(setImmediate);
        root.WtpUi.populate(settings);root.WtpUi.select(true);
        assert.equal(root.WtpUi.selected(),false,'management never selects an RF backend');
        assert.equal(root.WtpUi.read()['Device ID'],settings['Device ID']);
        assert(calls.every(call=>(call.options.method||'GET')==='GET'));
        assert(calls.every(call=>!/jobs|devices\/use|wtp-endpoint/.test(call.url)));
        assert.deepEqual(writes,[],'management browsing preserves old preference storage');
        assert([...timers.values()].some(timer=>timer.ms===5000));
    }
    assert.doesNotMatch(markup,/id="fleet-use"|id="wtp_use"|Plain LAN/);
    assert.match(markup,/Device management/);
    console.log('Management-only catalog, explicit identity validation, legacy preferences, and no backend/job mutation passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
