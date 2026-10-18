// SPDX-License-Identifier: MIT
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "../data/wtp.js"), "utf8");
const markup = fs.readFileSync(path.join(__dirname, "../data/views/wtp-controls.php"), "utf8");
const {defaults} = require("../data/wtp.js");
class Field {
    constructor(tag="") {
        Object.assign(this, {tag, value:"", checked:false, hidden:false, disabled:false,
            children:[], listeners:{}, parentElement:{hidden:false}, classList:{contains:()=>false}});
    }
    get options() { return this.children.flatMap(child => child.tag === "optgroup" ? child.children : [child]); }
    append(child) { this.children.push(child); }
    replaceChildren() { this.children = []; }
    addEventListener(name, fn) { this.listeners[name] = fn; }
    setAttribute() {}
    closest() { return this.parentElement; }
    focus() {}
    fire(name) { return this.listeners[name]?.({target:this}); }
}
const tick = () => new Promise(setImmediate);
async function run() {
    const fields = new Map();
    for (const match of [...source.matchAll(/byId\("([^"]+)"\)/g), ...markup.matchAll(/id="([^"]+)"/g)])
        fields.set(match[1], new Field());
    for (const id of ["wtp-status-text", "wtp-output", "wtp-clock", "wtp-identity", "wtp-history"]) fields.set(id, new Field());
    const field = id => fields.get(id);
    const events = {}, requests = [], timers = new Map();
    let nextTimer = 0, identifyResolve, delayIdentify = false, failUsb = false;
    let usb = [{id:"usb-test",instance:"WsprryPico SERIAL-C",binding:"usb",state:"online",settings:{...defaults,
        Endpoint:"/dev/serial/by-id/pico-if02", "USB Serial":"SERIAL-C", "USB Vendor ID":51966, "USB Product ID":16402}}];
    const root = {
        document:{readyState:"loading", getElementById:field, createElement:tag=>new Field(tag),
            querySelectorAll:()=>[], addEventListener:(name, fn)=>{events[name]=fn;}},
        addEventListener() {}, confirm:()=>true,
        fetch:async (url, options={}) => {
            requests.push({url, options});
            let value;
            if (url.endsWith("/status")) value = {host:{selected:false}};
            else if (url.endsWith("/host/devices")) value = {scope:"wsprrypi-wtp-catalog/1",profiles:[],active_id:""};
            else if (url.endsWith("/host/discovery")) value = {available:false,reason:"Avahi unavailable",candidates:[],
                usb:{available:!failUsb,reason:failUsb?"USB inventory unavailable":"",candidates:usb}};
            else if (url.endsWith("/host/discovery/identify")) {
                if (delayIdentify) await new Promise(resolve=>{identifyResolve=resolve;});
                value = {device_id:"a".repeat(32),binding:"usb",authenticated:false};
            } else throw new Error(`Unexpected request: ${url}`);
            return {ok:true,headers:{get:()=> '"revision"'},json:async()=>value};
        }
    };
    vm.runInNewContext(source, {window:root, AbortController, console,
        setTimeout:(fn,ms)=>{timers.set(++nextTimer,{fn,ms});return nextTimer;}, clearTimeout:id=>timers.delete(id)});
    events.DOMContentLoaded();

    await tick(); await tick();
    assert.equal(field("fleet-device").options.length, 1);
    assert.match(field("fleet-device").options[0].textContent, /SERIAL-C.*USB/);
    assert.equal(field("wtp_use").checked, false);
    assert.equal(requests.filter(r=>r.options.method==="POST").length, 0, "listing must not open or save a device");
    const firstOption = field("fleet-device").options[0];
    await field("fleet-refresh").fire("click");
    assert.equal(field("fleet-device").options[0], firstOption, "unchanged refresh must not rebuild the open selector");
    field("fleet-device").value = "nearby:usb-test";
    field("fleet-device").fire("change");
    field("fleet-add").fire("click");
    await tick();
    for (const [id, expected] of [["fleet-method","manual"],["fleet-binding","usb"],
        ["fleet-path","/dev/serial/by-id/pico-if02"],["fleet-serial","SERIAL-C"],
        ["fleet-vendor","51966"],["fleet-product","16402"],["fleet-identity","a".repeat(32)]])
        assert.equal(field(id).value, expected, id);
    const identify = requests.find(r=>r.url.endsWith("/identify"));
    assert.equal(JSON.parse(identify.options.body).discovery_id, "usb-test");
    assert.match(field("fleet-editor-feedback").textContent, /USB Pico identified/);
    assert.equal(requests.filter(r=>r.options.method==="POST").length, 1, "identification must not save or select");
    // Refresh discovers removal without discarding a name or endpoint draft.
    field("fleet-name").value = "Pico C draft";
    usb = [];
    const poll = [...timers.values()].find(timer=>timer.ms===3000);
    assert(poll, "automatic refresh must be scheduled");
    await poll.fn(); await tick();
    assert.equal(field("fleet-name").value, "Pico C draft");
    assert.equal(field("fleet-path").value, "/dev/serial/by-id/pico-if02");
    assert.match(field("fleet-discovery-status").textContent, /No USB Pico/);
    assert.equal(field("wtp_use").checked, false);
    // An API failure does not leave an old USB observation selectable.
    failUsb = true;
    await field("fleet-refresh").fire("click");
    assert.match(field("fleet-discovery-status").textContent, /USB inventory unavailable/);
    // A late reply cannot populate a cancelled editor or a newer draft.
    usb = [{id:"usb-test",instance:"Pico C",binding:"usb",state:"online",settings:{...defaults,
        Endpoint:"/dev/ttyACM7","USB Serial":"SERIAL-C","USB Vendor ID":51966,"USB Product ID":16402}}];
    failUsb = false;
    await field("fleet-refresh").fire("click");
    field("fleet-device").value = "nearby:usb-test";
    field("fleet-device").fire("change");
    delayIdentify = true;
    field("fleet-add").fire("click");
    await tick();
    field("fleet-editor-cancel").fire("click");
    identifyResolve();
    await tick();
    assert.equal(field("fleet-editor").hidden, true);
    assert.equal(field("fleet-identity").value, "");
    assert.equal(field("wtp_use").checked, false);
    console.log("USB discovery UI: automatic list, populated identity, no implicit selection, unplug, errors and late replies passed");
}
run().catch(error=>{console.error(error);process.exitCode=1;});
