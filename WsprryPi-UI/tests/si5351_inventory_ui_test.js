"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const source = fs.readFileSync(path.join(__dirname, "../data/index.js"), "utf8");
const siteSource = fs.readFileSync(path.join(__dirname, "../data/site.js"), "utf8");
const begin = source.indexOf("function refreshSi5351Addresses(");
const end = source.indexOf("\nfunction ", begin + 1);
let backend = "gpio", bus = 1;
const field = { value: "0x61", disabled: false, replaceChildren() {}, setAttribute() {} };
const hint = { textContent: "" };
const requests = [], populations = [];
const context = {
    document: { getElementById: id => id === "si5351_i2c_address" ? field : hint },
    selectedTransmitBackend: () => backend,
    selectedI2cBusValue: () => bus,
    formatSi5351Address: String,
    normalizedSi5351Addresses: list => Array.isArray(list) ? list : [],
    Option: function () {},
    si5351AddressDiscoverySequence: 0,
    si5351AddressInventoryState: {},
    SI5351_ADDRESSES_ENDPOINT: "/inventory",
    CONFIG_REQUEST_TIMEOUT_MS: 5000,
    window: { WSPRRYPI_PLATFORM: {} },
    validateTransmitterHardwareFields() {}, validatePage() {}, scheduleAutosave() {},
    populateSi5351Addresses: (...args) => populations.push(args),
    ajaxWithEndpointFallback(endpoint, options) {
        const request = { endpoint, options };
        requests.push(request);
        return { done(callback) { request.done = callback; return this; },
                 fail(callback) { request.fail = callback; return this; } };
    },
};
vm.createContext(context);
vm.runInContext(source.slice(begin, end), context);
// Execute the actual selection handler and population calls: loading an active
// Si5351 config must not scan the old bus before its saved settings are ready.
let backendChange;
context.clickTransmitBackend = () => {};
context.transmitBackendForUi = value => value === "si5351" ? "si5351" : "gpio";
context.$ = () => ({
    on(event, callback) { backendChange = callback; },
    prop(name, checked) { backend = checked ? "si5351" : "gpio"; return this; },
    trigger(event, extra) { backendChange({}, ...(extra || [])); },
});
const handlerStart = source.indexOf('    $("#transmit_backend").on("change"');
vm.runInContext(source.slice(handlerStart, source.indexOf('    $("#tx_pin").on', handlerStart)), context);
const selectionStart = source.indexOf("function setTransmitBackendSelection(");
vm.runInContext(source.slice(selectionStart, source.indexOf("\nfunction ", selectionStart + 1)), context);
context.transmitBackend = "si5351";
context.si5351I2cBus = 1;
context.si5351I2cAddressRaw = "0x61";
bus = 0;
vm.runInContext(siteSource.match(/setTransmitBackendSelection\(transmitBackend[^;]*;/)[0], context);
assert.equal(requests.length, 0, "configuration population defers inventory until saved bus/address are ready");
bus = 1;
const refreshStart = siteSource.indexOf('                    if (selectedTransmitBackend() === "si5351" &&');
vm.runInContext(siteSource.slice(refreshStart, siteSource.indexOf("                    // Enable the form", refreshStart)), context);
assert.equal(requests.length, 1, "active Si5351 configuration starts exactly one inventory scan");
assert.equal(requests[0].options.data.bus, 1);
requests.length = 0;
context.setTransmitBackendSelection("si5351", true);
assert.equal(requests.length, 1, "operator backend selection still refreshes inventory");
requests.length = 0;
for (backend of ["gpio", "wtp", "rp1-gpclk"]) context.refreshSi5351Addresses(1);
assert.equal(requests.length, 0, "inactive Si5351 never scans during configuration population");
assert.equal(field.value, "0x61", "inactive saved address is preserved");
backend = "si5351";
context.refreshSi5351Addresses(1);
assert.equal(requests.length, 1);
backend = "gpio";
requests[0].done({ "I2C Bus": 1, Addresses: ["0x60"], "Discovery Error": "" });
assert.equal(populations.length, 0, "reply after switching to GPIO cannot overwrite its draft");
backend = "si5351";
context.refreshSi5351Addresses(1);
bus = 2;
context.refreshSi5351Addresses(2);
requests[1].fail();
assert.equal(populations.length, 0, "older failure cannot replace newer bus state");
requests[2].done({ "I2C Bus": 2, Addresses: [], "Discovery Error": "Si5351 address discovery timed out" });
assert.equal(populations.length, 1);
assert.match(populations[0][3], /timed out/);
assert.equal(context.window.WSPRRYPI_PLATFORM.si5351Detected, false);
assert.equal(populations[0][1], "0x61", "timeout preserves the saved address");
console.log("Si5351 single-load scan, inactive-backend, late-reply and timeout UI tests passed");
