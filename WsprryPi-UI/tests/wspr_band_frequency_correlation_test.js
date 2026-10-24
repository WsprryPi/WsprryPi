"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const uiRoot = path.resolve(__dirname, "..");
const siteScript = fs.readFileSync(path.join(uiRoot, "data/site.js"), "utf8");
const indexScript = fs.readFileSync(path.join(uiRoot, "data/index.js"), "utf8");
const maintenanceView = fs.readFileSync(path.join(uiRoot, "data/views/maintenance.php"), "utf8");
const maintenanceStyles = fs.readFileSync(path.join(uiRoot, "data/maintenance.css"), "utf8");

assert.doesNotMatch(
    siteScript,
    /const\s+bandFrequencies\s*=/,
    "configured WSPR aliases must not retain a UI-owned frequency table"
);
for (const id of [
    "testToneSourceBand",
    "testToneSourceCustom",
    "testToneBand",
    "testToneFrequencyHz",
    "testToneSelectionPreview",
    "testToneSelectionError",
    "testToneExecutionResult",
]) {
    assert.match(maintenanceView, new RegExp(`id="${id}"`), `${id} markup must remain available`);
}
assert.match(maintenanceView, /id="testToneExecutionResult"[\s\S]*role="status"[\s\S]*aria-live="polite"/,
    "execution results must be announced separately from the requested preview");
assert.match(maintenanceStyles, /#testToneModal \.modal-footer \.btn:disabled[\s\S]*opacity:/,
    "Test Tone action buttons must visibly distinguish disabled state");

class FakeWebSocket {
    static OPEN = 1;
    static CONNECTING = 0;

    constructor(url) {
        this.url = url;
        this.readyState = FakeWebSocket.CONNECTING;
        this.listeners = Object.create(null);
        this.sent = [];
        this.sendAttempts = 0;
        this.throwOnSend = FakeWebSocket.throwOnNextSend ? 1 : 0;
        FakeWebSocket.throwOnNextSend = false;
        FakeWebSocket.instances.push(this);
    }

    addEventListener(type, listener) {
        (this.listeners[type] ||= []).push(listener);
    }

    emit(type, event = {}) {
        for (const listener of this.listeners[type] || []) listener(event);
    }

    open() {
        this.readyState = FakeWebSocket.OPEN;
        this.emit("open");
    }

    message(value) {
        this.emit("message", { data: typeof value === "string" ? value : JSON.stringify(value) });
    }

    close() {
        this.readyState = 3;
        this.emit("close", { code: 1006 });
    }

    send(payload) {
        this.sendAttempts += 1;
        if (this.throwOnSend > 0) {
            this.throwOnSend -= 1;
            throw new Error("mock send failure");
        }
        this.sent.push(JSON.parse(payload));
    }
}
FakeWebSocket.instances = [];
FakeWebSocket.throwOnNextSend = false;

let modalShown = false;
let guardModalShowCount = 0;
const diagnostics = {
    throwCount: 0,
    calls: [],
};
function classList() {
    const values = new Set();
    return {
        add(...names) { names.forEach((name) => values.add(name)); },
        remove(...names) { names.forEach((name) => values.delete(name)); },
        contains(name) { return values.has(name); },
        toggle(name, force) {
            if (force === undefined) {
                values.has(name) ? values.delete(name) : values.add(name);
            } else if (force) {
                values.add(name);
            } else {
                values.delete(name);
            }
            return values.has(name);
        },
        toString() { return [...values].join(" "); },
    };
}
function element(initial = {}) {
    return Object.assign({
        value: "",
        checked: false,
        disabled: false,
        textContent: "",
        options: [],
        childNodes: [],
        dataset: {},
        style: {},
        offsetWidth: 120,
        classList: classList(),
        handlers: Object.create(null),
        domListeners: Object.create(null),
        addEventListener(type, callback, options = {}) {
            (this.domListeners[type] ||= []).push({ callback, once: options.once === true });
        },
        dispatchEvent(event) {
            const listeners = this.domListeners[event.type] || [];
            this.domListeners[event.type] = listeners.filter(listener => !listener.once);
            for (const listener of listeners) listener.callback.call(this, event);
        },
        setAttribute(name, value) {
            this[name] = String(value);
        },
        removeAttribute(name) {
            delete this[name];
        },
        cloneNode() {
            return element({ textContent: this.textContent });
        },
        replaceChildren(...nodes) {
            this.options = [];
            this.childNodes = nodes;
            if (nodes.length === 0) this.value = "";
        },
        appendChild(option) {
            this.options.push(option);
            this.childNodes.push(option);
        },
    }, initial);
}
let elements;
let buttons;
function resetElements() {
    modalShown = false;
    guardModalShowCount = 0;
    elements = {
        testToneStart: element({ disabled: true }),
        testToneEnd: element({ disabled: true }),
        testToneClose: element(),
        testToneSourceBand: element(),
        testToneSourceCustom: element(),
        testToneBand: element({ disabled: true }),
        testToneFrequencyHz: element(),
        testToneFrequencyContext: element(),
        testToneSelectionPreview: element(),
        testToneSelectionError: element(),
        testToneExecutionResult: element(),
        test_tone: element(),
        testToneModal: element({ classList: { contains: (name) => name === "show" && modalShown } }),
        connIcon: element(),
        connStatusText: element(),
        confirmModal: element(),
        confirmModalLabel: element(),
        confirmModalBody: element(),
        confirmCancelBtn: element(),
        confirmActionBtn: element(),
        modeChangeGuardModal: element({
            querySelector(selector) {
                return selector === ".btn-close" ? elements.modeChangeGuardCloseBtn : null;
            },
        }),
        modeChangeGuardModalLabel: element(),
        modeChangeGuardModalBody: element(),
        modeChangeGuardConfirmBtn: element(),
        modeChangeGuardCancelBtn: element(),
        modeChangeGuardCloseBtn: element(),
    };
    buttons = {
        "#test_tone": elements.test_tone,
        "#testToneStart": elements.testToneStart,
        "#testToneEnd": elements.testToneEnd,
        "#testToneClose": elements.testToneClose,
    };
}
resetElements();

const timers = new Map();
const intervals = new Map();
const timerCallbacks = new Map();
const clearedTimers = new Set();
let timerGeneration = 0;
let nextTimerId = 1;
function scheduleTimer(kind, callback, delay) {
    const id = nextTimerId++;
    const record = { callback, delay, generation: timerGeneration, kind, state: "active" };
    (kind === "interval" ? intervals : timers).set(id, record);
    timerCallbacks.set(id, record);
    return id;
}
function schedule(callback, delay) {
    return scheduleTimer("timeout", callback, delay);
}
function scheduleInterval(callback, delay) {
    return scheduleTimer("interval", callback, delay);
}
function clearSchedule(id) {
    const record = timers.get(id) || intervals.get(id) || timerCallbacks.get(id);
    if (record) record.state = "canceled";
    clearedTimers.add(id);
    timers.delete(id);
    intervals.delete(id);
}
function runTimer(id) {
    const timer = timers.get(id);
    assert.ok(timer && timer.generation === timerGeneration,
        `timer ${id} must be pending in the current scenario before it runs`);
    timers.delete(id);
    timer.state = "fired";
    timer.callback();
}
function invokeRecordedTimer(id) {
    const record = timerCallbacks.get(id);
    if (!record || record.generation !== timerGeneration) return false;
    record.callback();
    return true;
}
function resetTimers() {
    timerGeneration += 1;
    timers.clear();
    intervals.clear();
    timerCallbacks.clear();
    clearedTimers.clear();
    nextTimerId = 1;
}

function jquery(selector) {
    const targets = (selector === vmContext.window || selector?.window === selector)
        ? [vmContext.window]
        : selector && typeof selector === "object" && selector.handlers
            ? [selector]
        : String(selector).split(",").map((value) => value.trim())
        .map((value) => buttons[value] || elements[value.replace(/^#/, "")])
        .filter(Boolean);
    return {
        length: targets.length,
        prop(name, value) {
            if (arguments.length === 1) return targets[0]?.[name];
            for (const target of targets) target[name] = value;
            return this;
        },
        off(eventName) {
            for (const target of targets) {
                if (!eventName) {
                    target.handlers = Object.create(null);
                    continue;
                }
                for (const token of String(eventName).split(/\s+/).filter(Boolean)) {
                    const [type, ...namespaces] = token.split(".");
                    const namespace = namespaces.join(".");
                    for (const registeredType of Object.keys(target.handlers)) {
                        if (type && registeredType !== type) continue;
                        target.handlers[registeredType] = target.handlers[registeredType].filter(
                            (entry) => namespace && entry.namespace !== namespace
                        );
                        if (target.handlers[registeredType].length === 0) {
                            delete target.handlers[registeredType];
                        }
                    }
                }
            }
            return this;
        },
        on(eventName, handler) {
            for (const token of String(eventName).split(/\s+/).filter(Boolean)) {
                const [type, ...namespaces] = token.split(".");
                const namespace = namespaces.join(".");
                for (const target of targets) {
                    (target.handlers[type] ||= []).push({ namespace, handler });
                }
            }
            return this;
        },
        one(eventName, handler) {
            return this.on(eventName, handler);
        },
        is() { return false; },
        text(value) {
            if (arguments.length === 0) return targets[0]?.textContent;
            for (const target of targets) target.textContent = value;
            return this;
        },
        attr(name, value) {
            if (arguments.length === 1) return targets[0]?.[name];
            for (const target of targets) target.setAttribute(name, value);
            return this;
        },
        toggleClass(name, force) {
            for (const target of targets) target.classList.toggle(name, force);
            return this;
        },
    };
}

function dispatch(target, eventName, event = {}) {
    const type = String(eventName).split(".")[0];
    const handlers = [...(target.handlers[type] || [])];
    for (const { handler } of handlers) {
        handler.call(target, event);
    }
}

function handlerCount(target, eventName) {
    const type = String(eventName).split(".")[0];
    return (target.handlers[type] || []).length;
}

const testConsole = {
    log(...args) {
        diagnostics.calls.push(args);
        if (diagnostics.throwCount > 0) {
            diagnostics.throwCount -= 1;
            throw new Error("mock post-send diagnostic failure");
        }
    },
    warn(...args) { this.log(...args); },
    error(...args) { this.log(...args); },
    debug(...args) { this.log(...args); },
};

let vmContext;
let context = {
    Array,
    JSON,
    Math,
    Number,
    Object,
    String,
    URL,
    URLSearchParams,
    WeakSet,
    WebSocket: FakeWebSocket,
    CwTimingState: { MORSE_TABLE: {} },
    console: testConsole,
    $: jquery,
    navigator: { onLine: true },
    document: {
        addEventListener() {},
        getElementById(id) {
            return elements[id] || null;
        },
        createElement() { return element(); },
        querySelector() { return null; },
        querySelectorAll() { return []; },
    },
    bootstrap: {
        Modal: class {
            static getOrCreateInstance() {
                return new this();
            }
            show() {
                modalShown = true;
                guardModalShowCount += 1;
            }
            hide() {
                modalShown = false;
            }
        },
        Tooltip: class {
            static getInstance() { return null; }
            constructor() {}
            setContent() {}
        },
    },
    recordedTimerCallbacks: timerCallbacks,
    setTimeout: schedule,
    clearTimeout: clearSchedule,
    WSPRRYPI_TEST_HOOKS: { enabled: true },
};
context.window = context;
context.window.setTimeout = schedule;
context.window.clearTimeout = clearSchedule;
context.window.setInterval = scheduleInterval;
context.window.clearInterval = clearSchedule;
context.location = context.window.location = {
    origin: "http://test.invalid",
    href: "http://test.invalid/maintenance.php",
    protocol: "http:",
    hostname: "test.invalid",
};
context.window.WSPRRYPI_PATHS = { socketPath: "/socket" };
context.window.WSPRRYPI_VIEW = "";
context.window.addEventListener = function addEventListener(type, listener) {
    (this.handlers ||= Object.create(null))[type] = listener;
};
vm.createContext(context);
vmContext = context;
vm.runInContext(siteScript, context, { filename: "data/site.js" });
vm.runInContext(indexScript, context, { filename: "data/index.js" });

const bridge = vmContext.WSPRRYPI_TEST_HOOKS.bridge;
assert.ok(bridge, "the guarded production test bridge must install when explicitly enabled");
assert.ok(vmContext.window.handlers.beforeunload,
    "complete site.js evaluation must register its production unload lifecycle handler");
assert.ok(vmContext.window.handlers.pagehide,
    "complete site.js evaluation must register its production page-hide lifecycle handler");
assert.ok(vmContext.window.handlers.load,
    "complete site.js evaluation must register its production load handler");
assert.match(siteScript,
    /WSPRRYPI_TEST_HOOKS[\s\S]*hooks\.enabled !== true/,
    "the test bridge must remain inert without an explicit enabled hook");

const productionFunctionIdentities = Object.freeze({
    showModeChangeGuardModal: vmContext.showModeChangeGuardModal,
    getTxState: bridge.functions.getTxState,
    setConnectionState: bridge.functions.setConnectionState,
    syncConnectionAlert: bridge.functions.syncConnectionAlert,
    armOutageBannerIfReady: bridge.functions.armOutageBannerIfReady,
    reloadAllData: bridge.functions.reloadAllData,
    toggleButtonLoading: bridge.functions.toggleButtonLoading,
    debugConsole: bridge.functions.debugConsole,
    showTestToneBlockedModal: bridge.functions.showTestToneBlockedModal,
    onTestToneStart: bridge.functions.onTestToneStart,
    onTestToneEnd: bridge.functions.onTestToneEnd,
    handleTestToneCommandResponse: bridge.functions.handleTestToneCommandResponse,
});

const initializationTimerBaseline = Object.freeze({
    timeouts: timers.size,
    intervals: intervals.size,
});
assert.ok(initializationTimerBaseline.intervals > 0,
    "complete script initialization must establish its polling-interval baseline before scenarios");
// The harness executes lifecycle registration but never runs production polling.
// Discard that immutable initialization baseline before the first scenario.
resetTimers();

function assertProductionFunctionIdentities() {
    for (const [name, reference] of Object.entries(productionFunctionIdentities)) {
        const current = name === "showModeChangeGuardModal"
            ? vmContext.showModeChangeGuardModal
            : bridge.functions[name];
        assert.equal(current, reference,
            `${name} must retain its production function identity for the full behavioral suite`);
    }
}

function resetScenarioEnvironment() {
    for (const socket of FakeWebSocket.instances) {
        socket.readyState = 3;
        socket.listeners = Object.create(null);
    }
    FakeWebSocket.instances = [];
    FakeWebSocket.throwOnNextSend = false;
    diagnostics.throwCount = 0;
    diagnostics.calls.length = 0;
    bridge.reset();
    resetTimers();
    resetElements();
    bridge.clearConnectionRecoveryState();
    bridge.setConfiguration("WSPR", "30m", 0);
    bridge.functions.bindTestToneControls();
    assert.equal(FakeWebSocket.instances.length, 0,
        "each scenario must begin with no fake WebSocket instances");
    assert.equal(timers.size, 0, "each scenario must begin with no active timeouts");
    assert.equal(intervals.size, 0, "each scenario must begin with no active intervals");
    assert.equal(bridge.inspect().catalog.authorized, false,
        "each scenario must begin without catalog authorization");
    assert.equal(bridge.inspect().testToneStart.quarantined, false,
        "each scenario must begin without a Start quarantine");
    assert.equal(handlerCount(elements.testToneStart, "click"), 1,
        "each scenario must rebind exactly one production Start handler");
    assert.equal(handlerCount(elements.testToneBand, "change"), 1,
        "each scenario must rebind exactly one production band-change handler");
    assertProductionFunctionIdentities();
}

let scenarioCount = 0;
function runScenario(name, callback) {
    scenarioCount += 1;
    resetScenarioEnvironment();
    try {
        callback();
    } catch (error) {
        error.message = `Scenario ${scenarioCount} (${name}): ${error.message}`;
        throw error;
    }
    for (const record of [...timers.values(), ...intervals.values()]) {
        assert.equal(record.generation, timerGeneration,
            "a scenario must not retain a timer record from an earlier generation");
    }
    for (const socket of FakeWebSocket.instances) {
        assert.ok(socket.listeners && typeof socket.listeners === "object",
            "a scenario must leave only live fake-socket listener records for its own generation");
    }
    assertProductionFunctionIdentities();
}

const harness = {
    catalogSnapshot() {
    const snapshot = bridge.inspect().catalog;
    return {
        ...snapshot,
        catalog: snapshot.dialFrequenciesHz,
    };
    },
    openConnection() {
    bridge.clearConnectionRecoveryState();
    const socket = bridge.functions.connectWebSocket("ws://test");
    socket.open();
    return socket;
    },
    createOperationConfigSnapshot: bridge.functions.createOperationConfigSnapshot,
    parseConfiguredWsprFrequencyHz: bridge.functions.parseConfiguredWsprFrequencyHz,
    updateWsprBandCatalog: bridge.functions.updateWsprBandCatalog,
    validateWsprBandCatalog: bridge.functions.validateWsprBandCatalog,
    createTestToneSelection: bridge.functions.createTestToneSelection,
    createTestToneSelectionPreview: bridge.functions.createTestToneSelectionPreview,
    testToneDefaultTransmitFrequencyHz: bridge.functions.testToneDefaultTransmitFrequencyHz,
    testToneFrequencyContextText: bridge.functions.testToneFrequencyContextText,
    clickTestTone: bridge.functions.clickTestTone,
    setTestToneInterlocks: bridge.setRuntimeInterlocks,
    syncTestToneControlState: bridge.functions.syncTestToneControlState,
    requestWsprBandCatalog: bridge.functions.requestWsprBandCatalog,
    initializeTestToneSelectionControls: bridge.functions.initializeTestToneSelectionControls,
    renderTestToneSelection: bridge.functions.renderTestToneSelection,
    setTestToneConfiguration: bridge.setConfiguration,
    currentTestToneSelection: () => bridge.inspect().selection,
    onTestToneStart: bridge.functions.onTestToneStart,
    clearPendingTestToneStartRequest: bridge.functions.clearPendingTestToneStartRequest,
    markPendingTestToneStartRequest: bridge.functions.markPendingTestToneStartRequest,
    pendingTestToneStartSource: () => bridge.inspect().testToneStart.source,
    testToneStartSnapshot: () => bridge.inspect().testToneStart,
    testToneLifecycleSnapshot: () => bridge.inspect().testToneLifecycle,
    clearTestToneExecutionResult: bridge.functions.clearTestToneExecutionResult,
    handleTestToneCommandResponse: bridge.functions.handleTestToneCommandResponse,
    bindTestToneControls: bridge.functions.bindTestToneControls,
    toneStartMessages(socket) {
        assert.ok(socket, "toneStartMessages requires the scenario-owned fake socket");
        return socket.sent.filter(
    (message) => message.command === "tone_start"
        );
    },
    invokeRecordedTimer,
};
context = harness;

assert.equal(bridge.functions.onTestToneStart, context.onTestToneStart,
    "the bridge must expose the real production Start handler reference");
for (const value of [
    bridge.inspect(),
    bridge.hasCurrentSocket(),
    bridge.currentSocketReadyState(),
]) {
    assert.equal(typeof value?.send, "undefined",
        "bridge return values must not expose transport send access");
    assert.equal(typeof value?.addEventListener, "undefined",
        "bridge return values must not expose transport listener registration");
    assert.equal(typeof value?.removeEventListener, "undefined",
        "bridge return values must not expose transport listener removal");
}
assert.equal(typeof bridge.currentSocket, "undefined",
    "the bridge must not expose the live socket object");

const jqueryDuplicateProbe = element();
let jqueryDuplicateProbeCalls = 0;
jquery(jqueryDuplicateProbe)
    .on("click.duplicateProbe", () => { jqueryDuplicateProbeCalls += 1; })
    .on("click.duplicateProbe", () => { jqueryDuplicateProbeCalls += 1; });
assert.equal(handlerCount(jqueryDuplicateProbe, "click"), 2,
    "the jQuery mock must retain duplicate registrations until explicitly removed");
dispatch(jqueryDuplicateProbe, "click");
assert.equal(jqueryDuplicateProbeCalls, 2,
    "the jQuery mock dispatch must invoke duplicate handlers in registration order");
jquery(jqueryDuplicateProbe).off(".duplicateProbe");
assert.equal(handlerCount(jqueryDuplicateProbe, "click"), 0,
    "namespaced off must remove every matching duplicate registration");

const canonicalBands = [
    "2200m", "630m", "160m", "80m", "60m", "40m", "30m",
    "20m", "17m", "15m", "12m", "10m", "6m", "4m", "2m", "1.25m", "70cm"
];
function validCatalog(offset = 1500) {
    const dialFrequenciesHz = [
        136000, 474200, 1836600, 3568600, 5287200, 7038600, 10138700,
        14095600, 18104600, 21094600, 24924600, 28124600, 50293000, 70091000,
        144489000, 222100000, 432300000,
    ];
    return {
        command: "wspr_band_catalog",
        status: "ok",
        audio_offset_hz: offset,
        frequency_profile: "existing_common",
        band_preferences: {},
        bands: canonicalBands.map((band, index) => {
            const dial = dialFrequenciesHz[index];
            return {
                band,
                dial_frequency_hz: dial,
                tone_frequency_hz: dial + offset,
                resolution_source: "built_in_preset",
                preset: band
            };
        }),
        presets: canonicalBands.map((band, index) => ({
            preset: band,
            band,
            dial_frequency_hz: dialFrequenciesHz[index],
            existing_common: true
        })).concat([
            { preset: "60m:legacy", band: "60m", dial_frequency_hz: 5287200, existing_common: true },
            { preset: "60m:wrc15", band: "60m", dial_frequency_hz: 5364700, existing_common: false }
        ]),
    };
}
function assertStartDisabled(message) {
    assert.equal(buttons["#testToneStart"].disabled, true, message);
}
function commandMessages(socket, command) {
    return socket.sent.filter((message) => message.command === command);
}

runScenario("pure catalog validation and selection helpers", () => {
const operationSnapshotBeforeCatalog = context.createOperationConfigSnapshot({
    mode: "WSPR",
    transmit: false,
    transmitBackend: "si5351",
    enableOnBoot: "Never",
    callsign: "NXXX",
    gridsquare: "ZZ99",
    wsprFrequencyValue: "30m",
    cwBaseFrequencyHz: 14096900,
    cwOffsetHz: 5,
});
assert.equal(operationSnapshotBeforeCatalog.wsprFrequencyHz, 0,
    "Operation config loading must use a safe unavailable WSPR frequency before catalog validation");
assert.equal(operationSnapshotBeforeCatalog.cwBaseFrequencyHz, 14096900,
    "Operation config loading must preserve the configured CW base frequency");
assert.equal(context.parseConfiguredWsprFrequencyHz("30m"), 0,
    "catalog aliases must remain unavailable before validation");
assert.equal(context.parseConfiguredWsprFrequencyHz("14.0956MHz"), 14095600,
    "numeric configured frequencies remain independent of catalog aliases");

const invalidCatalogCases = [
    ["missing row", (() => { const value = validCatalog(); value.bands.pop(); return value; })()],
    ["extra row", (() => { const value = validCatalog(); value.bands.push(value.bands[0]); return value; })()],
    ["duplicate row", (() => { const value = validCatalog(); value.bands[1].band = "2200m"; return value; })()],
    ["reordered row", (() => { const value = validCatalog(); [value.bands[0], value.bands[1]] = [value.bands[1], value.bands[0]]; return value; })()],
    ["lf row", (() => { const value = validCatalog(); value.bands[0].band = "lf"; return value; })()],
    ["mf row", (() => { const value = validCatalog(); value.bands[1].band = "mf"; return value; })()],
    ["invalid offset string", (() => { const value = validCatalog(); value.audio_offset_hz = "1500"; return value; })()],
    ["invalid offset Boolean", (() => { const value = validCatalog(); value.audio_offset_hz = false; return value; })()],
    ["invalid offset fractional", (() => { const value = validCatalog(); value.audio_offset_hz = 1500.5; return value; })()],
    ["invalid frequency profile", (() => { const value = validCatalog(); value.frequency_profile = "automatic"; return value; })()],
    ["invalid dial string", (() => { const value = validCatalog(); value.bands[0].dial_frequency_hz = "136000"; return value; })()],
    ["invalid dial Boolean", (() => { const value = validCatalog(); value.bands[0].dial_frequency_hz = true; return value; })()],
    ["invalid dial fractional", (() => { const value = validCatalog(); value.bands[0].dial_frequency_hz = 136000.5; return value; })()],
    ["invalid tone null", (() => { const value = validCatalog(); value.bands[0].tone_frequency_hz = null; return value; })()],
    ["invalid tone Boolean", (() => { const value = validCatalog(); value.bands[0].tone_frequency_hz = false; return value; })()],
    ["invalid tone fractional", (() => { const value = validCatalog(); value.bands[0].tone_frequency_hz = 137500.5; return value; })()],
    ["wrong tone relation", (() => { const value = validCatalog(); value.bands[0].tone_frequency_hz += 1; return value; })()],
    ["backend error", { command: "wspr_band_catalog", status: "error", message: "unavailable" }],
    ["malformed response", { command: "wspr_band_catalog", status: "ok", audio_offset_hz: 1500, bands: {} }],
];
for (const [name, catalog] of invalidCatalogCases) {
    assert.equal(context.validateWsprBandCatalog(catalog), null, `${name} must reject the entire catalog`);
}

const selectionCatalog = context.validateWsprBandCatalog(validCatalog(2750));
const selectionCatalogBefore = JSON.stringify(selectionCatalog);
for (const band of canonicalBands) {
    const selection = context.createTestToneSelection("wspr_band", band, selectionCatalog);
    assert.equal(selection.valid, true, `${band} must create a valid WSPR-band selection`);
    assert.equal(selection.mode, "wspr_band", `${band} selection must retain its source`);
    assert.equal(selection.band, band, `${band} selection must retain its canonical band`);
    assert.equal(selection.payload.command, "tone_start", `${band} payload must start a tone`);
    assert.equal(selection.payload.frequency_source, "wspr_band", `${band} payload must be semantic`);
    assert.equal(selection.payload.band, band, `${band} payload must retain the canonical band`);
    assert.equal(Object.hasOwn(selection.payload, "frequency_hz"), false,
        `${band} payload must not send an exact RF override`);
    assert.equal(selection.toneFrequencyHz, selection.dialFrequencyHz + selection.audioOffsetHz,
        `${band} selection must apply the catalog offset exactly once`);
    assert.equal(Object.isFrozen(selection.payload), true, `${band} payload must be immutable`);
}
context.updateWsprBandCatalog(validCatalog(2750));
const operationSnapshotAfterCatalog = context.createOperationConfigSnapshot({
    mode: "WSPR",
    transmit: false,
    transmitBackend: "si5351",
    enableOnBoot: "Never",
    callsign: "NXXX",
    gridsquare: "ZZ99",
    wsprFrequencyValue: "30m",
    cwBaseFrequencyHz: 14096900,
    cwOffsetHz: 5,
});
assert.equal(operationSnapshotAfterCatalog.wsprFrequencyHz, 10138700,
    "Operation config loading must resolve the configured WSPR band from the validated catalog");
assert.equal(JSON.stringify(selectionCatalog), selectionCatalogBefore,
    "selection must not mutate the validated catalog input");

const bandPreview = context.createTestToneSelectionPreview(
    context.createTestToneSelection("wspr_band", "30m", selectionCatalog)
);
assert.equal(bandPreview.valid, true, "band preview must be presentation-ready");
assert.equal(bandPreview.band, "30m", "band preview must retain the canonical band");
assert.equal(bandPreview.audioOffsetHz, 2750, "band preview must retain the non-default backend offset");
assert.equal(bandPreview.toneFrequencyHz, 10141450, "band preview must retain the final RF tone");
assert.match(bandPreview.text, /WSPR dial .*\+ 2750 Hz placement/, "band preview must explain dial and offset");

const customSelection = context.createTestToneSelection("custom_rf", "14097123", selectionCatalog);
assert.equal(customSelection.valid, true, "valid custom RF must create a selection");
assert.equal(customSelection.mode, "custom_rf", "custom RF must retain its source");
assert.equal(customSelection.frequencyHz, 14097123, "custom RF remains exact");
assert.equal(customSelection.payload.frequency_source, "custom_rf", "custom payload must be semantic");
assert.equal(customSelection.payload.frequency_hz, 14097123, "custom payload must carry exact RF");
assert.equal(Object.hasOwn(customSelection.payload, "band"), false,
    "custom payload must not send a band");
assert.equal(Object.hasOwn(customSelection, "audioOffsetHz"), false,
    "custom selection must not apply a WSPR offset");
const customPreview = context.createTestToneSelectionPreview(customSelection);
assert.equal(customPreview.valid, true, "custom preview must be presentation-ready");
assert.match(customPreview.text, /No WSPR or random placement/, "custom preview must state exact-RF semantics");
assert.match(customPreview.text, /backend validates the target/i,
    "custom preview must preserve backend authority");

for (const value of ["", " ", "+1", "-1", "1.5", "1e6", "14MHz", "0", "001", "9007199254740992", null, 14097123]) {
    const selection = context.createTestToneSelection("custom_rf", value, selectionCatalog);
    assert.equal(selection.valid, false, `invalid custom RF ${String(value)} must reject`);
    assert.equal(Object.hasOwn(selection, "payload"), false,
        `invalid custom RF ${String(value)} must not expose a payload`);
    assert.ok(selection.error, `invalid custom RF ${String(value)} must explain rejection`);
}
for (const [mode, value, catalog] of [
    ["wspr_band", "invalid", selectionCatalog],
    ["wspr_band", "20m", null],
    ["invalid_mode", "20m", selectionCatalog],
]) {
    const selection = context.createTestToneSelection(mode, value, catalog);
    assert.equal(selection.valid, false, `${mode} invalid input must reject`);
    assert.equal(Object.hasOwn(selection, "payload"), false, `${mode} invalid input must not expose a payload`);
}
assert.equal(context.createTestToneSelectionPreview({ valid: false, error: "invalid selection" }).valid, false,
    "invalid selections must have an invalid preview");
});

runScenario("authorized connection, controls, semantic requests, and responses", () => {
const first = context.openConnection();
assert.deepEqual(commandMessages(first, "wspr_band_catalog"), [{ command: "wspr_band_catalog" }],
    "every opened socket requests the catalog once");
assert.deepEqual(commandMessages(first, "get_tx_state"), [{ command: "get_tx_state" }],
    "the real getTxState function must read the open mock socket and request runtime state");
assert.equal(elements.connStatusText.textContent, "Controller connected",
    "the real connection-status function must update the mocked connection text");
assert.equal(elements.connIcon.classList.contains("state-connected"), true,
    "the real connection-status function must update the mocked connection icon state");
const firstPending = context.catalogSnapshot();
assert.equal(firstPending.pending, true, "catalog request is pending after open");
assert.equal(firstPending.authorized, false, "open cannot authorize a catalog");
assert.ok(timers.has(firstPending.timeoutHandle), "catalog timeout is armed on open");
assertStartDisabled("Start stays disabled while catalog loads");
context.clickTestTone.call(elements.test_tone, { preventDefault() {} });
assert.equal(elements.test_tone.disabled, true,
    "the real loading helper must disable the clicked Test Tone button while the modal opens");
assert.equal(elements.test_tone.style.width, "120px",
    "the real loading helper must preserve the clicked button width in the mocked DOM");
assertStartDisabled("opening the modal cannot enable Start before catalog authorization");

assert.equal(context.requestWsprBandCatalog(first), false, "same-socket duplicate catalog request is refused");
const afterDuplicateAttempt = context.catalogSnapshot();
assert.equal(commandMessages(first, "wspr_band_catalog").length, 1,
    "duplicate request sends no second catalog message");
assert.equal(afterDuplicateAttempt.connectionGeneration, firstPending.connectionGeneration,
    "duplicate request does not increment the connection generation");
assert.equal(afterDuplicateAttempt.requestGeneration, firstPending.requestGeneration,
    "duplicate request does not increment the request generation");
assert.equal(afterDuplicateAttempt.pendingRequestGeneration, firstPending.pendingRequestGeneration,
    "duplicate request preserves the original pending record");
assert.equal(afterDuplicateAttempt.timeoutHandle, firstPending.timeoutHandle,
    "duplicate request preserves the original timer");
assert.ok(timers.has(firstPending.timeoutHandle), "duplicate request does not rearm the timer");

const catalog2750 = validCatalog(2750);
first.message(catalog2750);
assert.equal(context.catalogSnapshot().authorized, true, "current valid response authorizes Start");
assert.equal(context.catalogSnapshot().pending, false, "valid response clears pending state");
assert.equal(timers.has(firstPending.timeoutHandle), false, "success clears the catalog timer");
assert.ok(clearedTimers.has(firstPending.timeoutHandle), "success cancels the catalog timer directly");
assert.equal(context.catalogSnapshot().offset, 2750, "non-default offset is retained from the catalog");
assert.equal(context.parseConfiguredWsprFrequencyHz("lf"), 136000,
    "lf redirects through the validated 2200m catalog row");
assert.equal(context.parseConfiguredWsprFrequencyHz("mf"), 474200,
    "mf redirects through the validated 630m catalog row");
assert.equal(context.parseConfiguredWsprFrequencyHz("30m"), 10138700,
    "canonical aliases resolve through the validated catalog");
assert.equal(context.testToneDefaultTransmitFrequencyHz(), 10141450,
    "non-default catalog offset is applied exactly once");
assert.equal(buttons["#testToneStart"].disabled, false, "Start enables only after current validation");
assert.deepEqual(elements.testToneBand.options.map((option) => option.value), ["", ...canonicalBands],
    "band selector must contain exactly the canonical catalog order without aliases");
assert.equal(elements.testToneSourceBand.checked, true, "configured 30m defaults to band mode");
assert.equal(elements.testToneBand.value, "30m", "configured 30m selects its canonical catalog row");
assert.match(elements.testToneSelectionPreview.textContent, /WSPR dial .*\+ 2750 Hz placement/,
    "band mode preview must show the catalog dial and non-default offset");

context.setTestToneConfiguration("WSPR", "20m", 0);
context.initializeTestToneSelectionControls();
assert.equal(elements.testToneSourceBand.checked, true, "configured 20m defaults to band mode");
assert.equal(elements.testToneBand.value, "20m", "configured 20m selects its canonical row");
context.setTestToneConfiguration("WSPR", "lf", 0);
context.initializeTestToneSelectionControls();
assert.equal(elements.testToneBand.value, "2200m", "lf defaults through the canonical 2200m row");
context.setTestToneConfiguration("WSPR", "mf", 0);
context.initializeTestToneSelectionControls();
assert.equal(elements.testToneBand.value, "630m", "mf defaults through the canonical 630m row");
context.setTestToneConfiguration("WSPR", "14.095600MHz", 0);
context.initializeTestToneSelectionControls();
assert.equal(elements.testToneBand.value, "20m", "canonical numeric dial defaults to band mode");
context.setTestToneConfiguration("QRSS", "", 14097123);
context.initializeTestToneSelectionControls();
assert.equal(elements.testToneSourceCustom.checked, true, "non-WSPR configured frequency defaults to custom mode");
assert.equal(elements.testToneFrequencyHz.value, "14097123", "custom default retains exact configured RF");
context.setTestToneConfiguration("WSPR", "not-a-band", 0);
context.initializeTestToneSelectionControls();
assert.equal(elements.testToneSourceBand.checked, false, "unavailable configuration does not choose an arbitrary band");
assert.equal(elements.testToneSourceCustom.checked, false, "unavailable configuration remains unselected");
assertStartDisabled("unavailable configuration keeps Start disabled");

context.setTestToneConfiguration("WSPR", "20m", 0);
context.initializeTestToneSelectionControls();
context.bindTestToneControls();
assert.equal(handlerCount(elements.testToneStart, "click"), 1, "Start must be bound exactly once");
assert.equal(elements.testToneStart.handlers.click[0].handler, bridge.functions.onTestToneStart,
    "the bound Start event must use the same production handler reference exposed by the bridge");
assert.equal(handlerCount(elements.testToneSourceCustom, "change"), 1, "mode changes must be bound");
assert.equal(handlerCount(elements.testToneBand, "change"), 1, "band changes must be bound");
assert.equal(handlerCount(elements.testToneFrequencyHz, "input"), 1, "custom input must be bound");
dispatch(elements.testToneStart, "click", { preventDefault() {} });
assert.equal(JSON.stringify(context.toneStartMessages(first)), JSON.stringify([{
    command: "tone_start",
    client_request_id: context.toneStartMessages(first)[0].client_request_id,
    frequency_source: "wspr_band",
    band: "20m",
}]), "band mode must send exactly the semantic band payload");
assert.equal(Object.hasOwn(context.toneStartMessages(first)[0], "frequency_hz"), false,
    "band mode must never include a legacy exact-RF override");
assert.equal(context.pendingTestToneStartSource(), "wspr_band",
    "the pending context must retain the exact semantic band source that was sent");
context.clearPendingTestToneStartRequest();

elements.testToneSourceBand.checked = false;
elements.testToneSourceCustom.checked = true;
elements.testToneFrequencyHz.value = "14097123";
dispatch(elements.testToneSourceCustom, "change", { preventDefault() {} });
dispatch(elements.testToneFrequencyHz, "input", { preventDefault() {} });
assert.match(elements.testToneSelectionPreview.textContent, /No WSPR or random placement/,
    "custom mode preview must state exact-RF offset behavior");
dispatch(elements.testToneStart, "click", { preventDefault() {} });
assert.equal(JSON.stringify(context.toneStartMessages(first)[1]), JSON.stringify({
    command: "tone_start",
    client_request_id: context.toneStartMessages(first)[1].client_request_id,
    frequency_source: "custom_rf",
    frequency_hz: 14097123,
}), "custom mode must send exactly the semantic exact-RF payload");
assert.equal(Object.hasOwn(context.toneStartMessages(first)[1], "band"), false,
    "custom mode must never include a band field");
assert.equal(context.pendingTestToneStartSource(), "custom_rf",
    "the pending context must retain the exact semantic custom source that was sent");
context.clearPendingTestToneStartRequest();
elements.testToneFrequencyHz.value = "1.5";
dispatch(elements.testToneFrequencyHz, "input", { preventDefault() {} });
assertStartDisabled("invalid custom input disables Start");
assert.match(elements.testToneSelectionError.textContent, /positive whole-number/i,
    "invalid custom input must be shown beside the selection controls");
assert.equal(elements.testToneSelectionPreview.textContent, "",
    "invalid selection guidance must render only once in the alert region");
context.syncTestToneControlState(true);
assert.equal(buttons["#testToneEnd"].disabled, false,
    "End remains available for an active tone despite invalid current selection");
context.syncTestToneControlState(false);
elements.testToneFrequencyHz.value = "14097123";
context.renderTestToneSelection();
context.setTestToneInterlocks(true, false);
context.syncTestToneControlState(false);
assertStartDisabled("active managed transmission remains an independent Start interlock");
context.setTestToneInterlocks(false, true);
context.syncTestToneControlState(false);
assertStartDisabled("enabled schedule remains an independent Start interlock");
context.setTestToneInterlocks(false, false);
context.syncTestToneControlState(false);
assert.equal(buttons["#testToneStart"].disabled, false, "Start returns only when all interlocks clear");

elements.testToneSourceBand.checked = true;
elements.testToneSourceCustom.checked = false;
elements.testToneBand.value = "20m";
context.renderTestToneSelection();
context.syncTestToneControlState(false);
const failedStartTimer = nextTimerId;
const sentBeforeThrow = context.toneStartMessages(first).length;
const sendAttemptsBeforeThrow = first.sendAttempts;
first.throwOnSend = true;
let startExceptionEscaped = false;
try {
    dispatch(elements.testToneStart, "click", { preventDefault() {} });
} catch (error) {
    startExceptionEscaped = true;
}
assert.equal(startExceptionEscaped, false,
    "a synchronous production WebSocket.send exception must not escape the bound Start handler");
assert.equal(first.sendAttempts, sendAttemptsBeforeThrow + 1,
    "the throwing Start path must attempt exactly one WebSocket send");
assert.equal(context.toneStartMessages(first).length, sentBeforeThrow,
    "a throwing WebSocket.send must not record a transmitted tone_start message");
assert.equal(timers.has(failedStartTimer), false,
    "the synchronous send failure must clear the exact Start timer immediately");
assert.ok(clearedTimers.has(failedStartTimer),
    "the synchronous send failure must cancel the exact Start timer directly");
assert.equal(JSON.stringify(context.testToneStartSnapshot()), JSON.stringify({
    pending: false,
    source: "",
    hasUnresolvedContext: false,
    quarantined: false,
    timeoutHandle: null,
}), "a definitely unsent Start request must not retain semantic context or quarantine its socket");
assert.match(elements.testToneExecutionResult.textContent, /could not be sent.*try again/i,
    "the synchronous send failure must be visible beside the Test Tone controls");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /WSPR dial|RF|GPIO|Selector/,
    "a definitely unsent Start request must not display committed execution details");
assert.equal(buttons["#testToneEnd"].disabled, true,
    "End remains unavailable when a throwing send never started a tone");
assert.equal(buttons["#testToneStart"].disabled, false,
    "the same open, authorized socket may retry after a definitely unsent Start request");
context.invokeRecordedTimer(failedStartTimer);
assert.equal(context.testToneStartSnapshot().quarantined, false,
    "a canceled Start timer callback cannot turn a definitely unsent request into an unknown-outcome quarantine");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /outcome is unknown/i,
    "a canceled Start timer callback must not replace the send-failure message");
first.throwOnSend = false;
dispatch(elements.testToneStart, "click", { preventDefault() {} });
assert.equal(context.toneStartMessages(first).length, sentBeforeThrow + 1,
    "a retry after synchronous send failure must use the normal semantic Start path");
context.clearPendingTestToneStartRequest();

selectBandForTimedStart("20m");
const sentBeforePostSendDebugFailure = context.toneStartMessages(first).length;
const sendAttemptsBeforePostSendDebugFailure = first.sendAttempts;
bridge.setConsoleLogLevelForTest("debug");
diagnostics.throwCount = 2;
let postSendDebugExceptionEscaped = false;
try {
    dispatch(elements.testToneStart, "click", { preventDefault() {} });
} catch (error) {
    postSendDebugExceptionEscaped = true;
}
assert.equal(postSendDebugExceptionEscaped, false,
    "a post-send diagnostic failure must not escape the bound Start handler");
assert.equal(first.sendAttempts, sendAttemptsBeforePostSendDebugFailure + 1,
    "a post-send diagnostic failure still has exactly one accepted WebSocket send");
assert.equal(context.toneStartMessages(first).length, sentBeforePostSendDebugFailure + 1,
    "the accepted Start request must remain recorded when later diagnostics fail");
assert.equal(diagnostics.throwCount, 0,
    "the real debugConsole function must reach the throwing diagnostic dependency after send acceptance");
assert.ok(diagnostics.calls.some((args) => args.some((value) => String(value).includes("Test tone start."))),
    "the real debugConsole function must deliver the Start diagnostic to the preinstalled console mock");
const postSendDebugPending = context.testToneStartSnapshot();
assert.equal(postSendDebugPending.pending, true,
    "post-send diagnostics must not clear the pending Start state");
assert.equal(postSendDebugPending.source, "wspr_band",
    "post-send diagnostics must retain immutable semantic request attribution");
assert.ok(timers.has(postSendDebugPending.timeoutHandle),
    "post-send diagnostics must leave the exact Start timeout armed");
assertStartDisabled("post-send diagnostics must not permit another Start request");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /could not be sent/i,
    "post-send diagnostics must not relabel an accepted request as unsent");
dispatch(elements.testToneStart, "click", { preventDefault() {} });
assert.equal(context.toneStartMessages(first).length, sentBeforePostSendDebugFailure + 1,
    "a second Start after post-send diagnostics must not send a duplicate request");
first.message({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "20m",
    dial_frequency_hz: 14095600,
    audio_offset_hz: 1500,
    actual_rf_frequency_hz: 14097100,
    selector_gpio_enabled: false,
});
assert.match(elements.testToneExecutionResult.textContent, /at 14\.097100 MHz RF \(requested values differed\)/i,
    "a response after post-send diagnostics must remain attributed and disclose changed execution values");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "End remains available after an accepted request receives its normal response");
context.handleTestToneCommandResponse({ command: "tone_end", stopped: true });

selectBandForTimedStart("20m");
diagnostics.throwCount = 2;
dispatch(elements.testToneStart, "click", { preventDefault() {} });
const postSendDebugTimeout = context.testToneStartSnapshot();
assert.equal(postSendDebugTimeout.pending, true,
    "a second accepted post-send diagnostic failure must still enter pending state");
assert.ok(timers.has(postSendDebugTimeout.timeoutHandle),
    "the pending request must retain its timer before the real timeout callback");
runTimer(postSendDebugTimeout.timeoutHandle);
assert.equal(JSON.stringify(context.testToneStartSnapshot()), JSON.stringify({
    pending: false,
    source: "wspr_band",
    hasUnresolvedContext: true,
    quarantined: true,
    timeoutHandle: null,
}), "a genuine timeout after post-send diagnostics must retain attribution and quarantine the socket");
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: false,
    message: "Timed-out test reset rejection.",
});

context.markPendingTestToneStartRequest({ frequency_source: "wspr_band", band: "20m" });
first.message({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "20m",
    dial_frequency_hz: 14095600,
    audio_offset_hz: 2750,
    actual_rf_frequency_hz: 14098350,
    selector_gpio_enabled: true,
    selector_gpio: 17,
    selector_gpio_active_high: true,
});
assert.match(elements.testToneExecutionResult.textContent, /Requested frequency committed/i,
    "matching band success must confirm execution without repeating the preview equation");
assert.match(elements.testToneExecutionResult.textContent, /GPIO 17, active high/,
    "band success must display committed active-high selector metadata");
assert.match(elements.testToneExecutionResult.className, /text-body/, "acceptance must be styled as pending runtime confirmation");
assert.equal(buttons["#testToneEnd"].disabled, false, "End remains usable after a successful start");

context.markPendingTestToneStartRequest({ frequency_source: "custom_rf", frequency_hz: 14097123 });
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: true,
    frequency_source: "custom_rf",
    band: "20m",
    actual_rf_frequency_hz: 14097123,
    selector_gpio_enabled: true,
    selector_gpio: 18,
    selector_gpio_active_high: false,
});
assert.match(elements.testToneExecutionResult.textContent, /Requested exact RF frequency committed/i,
    "matching custom success must confirm exact RF without repeating raw input");
assert.match(elements.testToneExecutionResult.textContent, /GPIO 18, active low/,
    "custom success must display committed active-low selector metadata");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /WSPR dial/, "custom success must omit dial metadata");

context.markPendingTestToneStartRequest({ frequency_source: "wspr_band" });
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "30m",
    dial_frequency_hz: 10138700,
    audio_offset_hz: 1500,
    actual_rf_frequency_hz: 10140200,
    selector_gpio_enabled: false,
});
assert.match(elements.testToneExecutionResult.textContent, /Selector: disabled/, "selector-disabled success must be explicit");

context.markPendingTestToneStartRequest({ frequency_source: "wspr_band" });
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "20m",
    dial_frequency_hz: 14095600,
    audio_offset_hz: 1500,
    selector_gpio_enabled: "false",
});
assert.match(elements.testToneExecutionResult.textContent, /committed execution details were unavailable or invalid/i,
    "malformed semantic success must warn without inventing committed values");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /14095600|GPIO/, "malformed success must not expose unvalidated metadata");

for (const [name, expectedSource, response] of [
    ["missing source", "wspr_band", { command: "tone_start", started: true }],
    ["unknown source", "wspr_band", { command: "tone_start", started: true, frequency_source: "invalid" }],
    ["band request with custom response", "wspr_band", { command: "tone_start", started: true, frequency_source: "custom_rf" }],
    ["custom request with band response", "custom_rf", { command: "tone_start", started: true, frequency_source: "wspr_band" }],
]) {
    context.markPendingTestToneStartRequest({ frequency_source: expectedSource });
    context.handleTestToneCommandResponse(response);
    assert.match(elements.testToneExecutionResult.textContent, /committed execution details were unavailable or invalid/i,
        `${name} must warn rather than be treated as legacy success`);
    assert.equal(buttons["#testToneEnd"].disabled, false,
        `${name} must keep End available because the backend reported an active tone`);
}

context.clearTestToneExecutionResult();
context.markPendingTestToneStartRequest();
context.handleTestToneCommandResponse({ command: "tone_start", started: true });
assert.equal(elements.testToneExecutionResult.textContent, "Test Tone accepted; waiting for runtime confirmation.",
    "a genuine legacy success must not invent semantic execution details or a malformed warning");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "a genuine legacy success must keep End available");

context.markPendingTestToneStartRequest();
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: false,
    message: "Requested band is unavailable.",
    blocked_by_active_transmission: false,
    blocked_by_enabled_transmission: false,
});
assert.equal(elements.testToneExecutionResult.textContent, "Requested band is unavailable.",
    "ordinary backend rejection must be shown beside the controls");
assert.match(elements.testToneExecutionResult.className, /text-danger/, "rejection must be visibly distinct");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /RF|GPIO|Selector/, "rejection must not display committed details");

context.markPendingTestToneStartRequest();
context.handleTestToneCommandResponse({ command: "tone_start", started: false, blocked_by_active_transmission: true });
assert.equal(elements.modeChangeGuardModalLabel.textContent, "Stop and disable transmissions",
    "the real Test Tone guard-modal flow must populate the active-transmission title");
assert.ok(guardModalShowCount > 0,
    "the real Test Tone guard-modal flow must reach the preinstalled Bootstrap modal mock");
assert.match(elements.testToneExecutionResult.textContent, /rejected by the controller/i,
    "missing rejection message must use a safe inline fallback");
context.markPendingTestToneStartRequest();
context.handleTestToneCommandResponse({ command: "tone_start", started: false, blocked_by_enabled_transmission: true, message: "Disable the schedule first." });
assert.equal(elements.modeChangeGuardModalLabel.textContent, "Disable transmissions",
    "the real Test Tone guard-modal flow must populate the enabled-schedule title");
assert.equal(elements.testToneExecutionResult.textContent, "Disable the schedule first.",
    "blocked rejection must retain its backend message inline");

first.message(validCatalog(1500));
assert.equal(context.catalogSnapshot().offset, 2750,
    "duplicate responses are ignored deterministically after authorization");
assert.equal(context.requestWsprBandCatalog(first), false,
    "same-socket request remains refused after the original request completes");
assert.equal(first.sent.filter((message) => message.command === "wspr_band_catalog").length, 1,
    "completed same-socket catalog request sends no new message");
});

function selectBandForTimedStart(band = "20m") {
    elements.testToneSourceBand.checked = true;
    elements.testToneSourceCustom.checked = false;
    elements.testToneBand.value = band;
    context.renderTestToneSelection();
    context.syncTestToneControlState(false);
}

function selectCustomForTimedStart(frequencyHz = "14097123") {
    elements.testToneSourceBand.checked = false;
    elements.testToneSourceCustom.checked = true;
    elements.testToneFrequencyHz.value = frequencyHz;
    context.renderTestToneSelection();
    context.syncTestToneControlState(false);
}

function openAuthorizedConnection(offset = 2750) {
    const socket = context.openConnection();
    socket.message(validCatalog(offset));
    assert.equal(context.catalogSnapshot().authorized, true,
        "an explicit timeout scenario must start from its own authorized connection");
    return socket;
}

function startSelectedToneAndRunRealTimeout(socket, expectedSource) {
    const sentBefore = context.toneStartMessages(socket).length;
    dispatch(elements.testToneStart, "click", { preventDefault() {} });
    assert.equal(context.toneStartMessages(socket).length, sentBefore + 1,
        "the bound Start handler must send exactly one semantic request before timeout");
    assert.equal(context.toneStartMessages(socket).at(-1).frequency_source, expectedSource,
        "the bound Start handler must retain the exact source it sent");
    const pending = context.testToneStartSnapshot();
    assert.equal(pending.pending, true, "a submitted Start request must enter pending state");
    assert.equal(pending.source, expectedSource, "pending context must retain the immutable semantic source");
    assert.ok(timers.has(pending.timeoutHandle), "the production Start timeout must be armed");
    runTimer(pending.timeoutHandle);
    return { sentBefore, pending };
}

function finishLateStartedTone() {
    context.handleTestToneCommandResponse({ command: "tone_end", stopped: true });
    assert.equal(buttons["#testToneEnd"].disabled, true,
        "the test reset must restore inactive End state after the late result");
}

runScenario("timeout quarantine and late semantic responses", () => {
const first = openAuthorizedConnection();
selectBandForTimedStart("20m");
const timedBandStart = startSelectedToneAndRunRealTimeout(first, "wspr_band");
assert.equal(JSON.stringify(context.testToneStartSnapshot()), JSON.stringify({
    pending: false,
    source: "wspr_band",
    hasUnresolvedContext: true,
    quarantined: true,
    timeoutHandle: null,
}), "the real timeout callback clears progress but retains and quarantines the unresolved semantic request");
assertStartDisabled("a timed-out unresolved Start request must quarantine this socket");
assert.match(elements.testToneExecutionResult.textContent, /timed out.*outcome is unknown.*response or reconnect/i,
    "timeout outcome guidance must be inline beside the Test Tone controls");
dispatch(elements.testToneStart, "click", { preventDefault() {} });
assert.equal(context.toneStartMessages(first).length, timedBandStart.sentBefore + 1,
    "a second Start attempt on the quarantined socket must send nothing");
first.message({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "20m",
    dial_frequency_hz: 14095600,
    audio_offset_hz: 2750,
    actual_rf_frequency_hz: 14098350,
    selector_gpio_enabled: false,
});
assert.match(elements.testToneExecutionResult.textContent, /Requested frequency committed/i,
    "a late valid band response must be attributed to its timed-out semantic request");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "End remains available after a late backend-confirmed start");
assert.equal(JSON.stringify(context.testToneStartSnapshot()), JSON.stringify({
    pending: false,
    source: "",
    hasUnresolvedContext: false,
    quarantined: false,
    timeoutHandle: null,
}), "a completed late response must release its retained context and socket quarantine");
finishLateStartedTone();

for (const [name, response] of [
    ["missing source", { command: "tone_start", started: true }],
    ["unknown source", { command: "tone_start", started: true, frequency_source: "unknown" }],
    ["mismatched source", { command: "tone_start", started: true, frequency_source: "custom_rf" }],
]) {
    selectBandForTimedStart("20m");
    startSelectedToneAndRunRealTimeout(first, "wspr_band");
    context.handleTestToneCommandResponse(response);
    assert.match(elements.testToneExecutionResult.textContent, /committed execution details were unavailable or invalid/i,
        `late ${name} must warn instead of being treated as a legacy result`);
    assert.equal(buttons["#testToneEnd"].disabled, false,
        `End must remain available after a late ${name} response reporting started`);
    finishLateStartedTone();
}

selectCustomForTimedStart("14097123");
startSelectedToneAndRunRealTimeout(first, "custom_rf");
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: true,
    frequency_source: "custom_rf",
    band: "20m",
    actual_rf_frequency_hz: 14097123,
    selector_gpio_enabled: true,
    selector_gpio: 18,
    selector_gpio_active_high: false,
});
assert.match(elements.testToneExecutionResult.textContent, /Requested exact RF frequency committed/i,
    "a late valid custom response must retain its exact RF semantics");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "End remains available after a late valid custom start");
finishLateStartedTone();

selectCustomForTimedStart("14097123");
startSelectedToneAndRunRealTimeout(first, "custom_rf");
context.handleTestToneCommandResponse({
    command: "tone_start",
    started: false,
    message: "The backend rejected the late custom request.",
});
assert.equal(elements.testToneExecutionResult.textContent, "The backend rejected the late custom request. This response cannot be correlated; use End to recover.",
    "a late rejection must preserve its backend message inline");
assert.doesNotMatch(elements.testToneExecutionResult.textContent, /RF|GPIO|Selector/,
    "a late rejection must not display uncommitted execution metadata");
assert.equal(context.testToneStartSnapshot().hasUnresolvedContext, true,
    "an uncorrelated legacy rejection must retain the recovery context");
assert.equal(context.testToneLifecycleSnapshot().state, "unknown");
assert.equal(elements.testToneEnd.disabled, false);
bridge.functions.onTestToneEnd.call(elements.testToneEnd, { preventDefault() {} });
context.handleTestToneCommandResponse({ command: "tone_end", stopped: true });

context.clearTestToneExecutionResult();
context.handleTestToneCommandResponse({ command: "tone_start", started: true });
assert.equal(elements.testToneExecutionResult.textContent, "Test Tone accepted; waiting for runtime confirmation.",
    "a genuine legacy success without retained semantic context remains compatible");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "a genuine legacy success remains truthfully active");
finishLateStartedTone();
});

runScenario("timed Start recovery End preserves and settles quarantine", () => {
const first = openAuthorizedConnection();
selectBandForTimedStart("20m");
startSelectedToneAndRunRealTimeout(first, "wspr_band");
assert.equal(context.testToneLifecycleSnapshot().state, "unknown",
    "a timed Start must retain an unknown lifecycle until the controller confirms an outcome");
assert.equal(context.testToneStartSnapshot().quarantined, true,
    "a timed Start must quarantine its current socket");
assert.equal(buttons["#testToneStart"].disabled, true,
    "a timed Start must keep Start disabled");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "a timed Start must leave End available for recovery");

dispatch(elements.testToneEnd, "click", { preventDefault() {} });
assert.equal(commandMessages(first, "tone_end").length, 1,
    "recovery End must be sent on the quarantined socket");
first.message({ command: "tone_end", stopped: true });
assert.equal(context.testToneLifecycleSnapshot().state, "idle",
    "a confirmed recovery End must settle the lifecycle");
assert.equal(JSON.stringify(context.testToneStartSnapshot()), JSON.stringify({
    pending: false,
    source: "",
    hasUnresolvedContext: false,
    quarantined: false,
    timeoutHandle: null,
}), "only a confirmed same-socket End may release the timed Start context and quarantine");
assert.equal(buttons["#testToneStart"].disabled, false,
    "a confirmed recovery End must restore Start when the catalog, selection, and interlocks permit it");

startSelectedToneAndRunRealTimeout(first, "wspr_band");
dispatch(elements.testToneEnd, "click", { preventDefault() {} });
first.message({ command: "tone_end", stopped: false, message: "Controller rejected End." });
assert.equal(context.testToneLifecycleSnapshot().state, "unknown",
    "a rejected End must retain possible active-tone state");
assert.equal(context.testToneStartSnapshot().quarantined, true,
    "a rejected End must retain the timed Start quarantine");
assert.equal(buttons["#testToneStart"].disabled, true,
    "a rejected End must keep Start disabled");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "a rejected End must leave recovery End available");

dispatch(elements.testToneEnd, "click", { preventDefault() {} });
const timedEnd = context.testToneLifecycleSnapshot().endTimeoutHandle;
assert.ok(timers.has(timedEnd), "a recovery End must use the production End timeout");
runTimer(timedEnd);
assert.equal(context.testToneLifecycleSnapshot().state, "unknown",
    "a timed-out End must retain the unknown outcome");
assert.equal(context.testToneStartSnapshot().quarantined, true,
    "a timed-out End must retain the timed Start quarantine");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "a timed-out End must remain available for recovery");

first.close();
const second = openAuthorizedConnection();
const replacementSnapshot = JSON.stringify({
    start: context.testToneStartSnapshot(),
    lifecycle: context.testToneLifecycleSnapshot(),
    startDisabled: buttons["#testToneStart"].disabled,
    endDisabled: buttons["#testToneEnd"].disabled,
});
first.message({ command: "tone_end", stopped: true });
assert.equal(JSON.stringify({
    start: context.testToneStartSnapshot(),
    lifecycle: context.testToneLifecycleSnapshot(),
    startDisabled: buttons["#testToneStart"].disabled,
    endDisabled: buttons["#testToneEnd"].disabled,
}), replacementSnapshot,
"a late confirmed End from an obsolete socket must not alter the replacement socket state");
second.close();
});

runScenario("End success, timeout, and catalog-loss lifecycle", () => {
const first = openAuthorizedConnection();
selectBandForTimedStart("20m");
dispatch(elements.testToneStart, "click", { preventDefault() {} });
first.message({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "20m",
    dial_frequency_hz: 14095600,
    audio_offset_hz: 1500,
    actual_rf_frequency_hz: 14097100,
    selector_gpio_enabled: false,
});
context.setTestToneInterlocks(true, false);
dispatch(elements.testToneEnd, "click", { preventDefault() {} });
assert.equal(commandMessages(first, "tone_end").length, 1,
    "End must send one command while the explicit lifecycle is active");
assert.equal(context.testToneLifecycleSnapshot().state, "end_pending",
    "End must enter an explicit pending state independent of button flags");
first.message({ command: "tone_end", stopped: true });
assert.equal(context.testToneLifecycleSnapshot().state, "idle",
    "a confirmed End must clear active-tone state");
assert.equal(buttons["#testToneStart"].disabled, false,
    "a confirmed End must restore Start despite the stale pre-End runtime snapshot");
assert.equal(elements.testToneExecutionResult.textContent, "Test Tone transaction ended; restoration details unavailable.",
    "a confirmed End must replace the stale Started result");

dispatch(elements.testToneStart, "click", { preventDefault() {} });
first.message({
    command: "tone_start",
    started: true,
    frequency_source: "wspr_band",
    band: "20m",
    dial_frequency_hz: 14095600,
    audio_offset_hz: 1500,
    actual_rf_frequency_hz: 14097100,
    selector_gpio_enabled: false,
});
dispatch(elements.testToneEnd, "click", { preventDefault() {} });
const endTimeout = context.testToneLifecycleSnapshot().endTimeoutHandle;
assert.ok(timers.has(endTimeout), "End must arm the production command timeout");
runTimer(endTimeout);
assert.equal(context.testToneLifecycleSnapshot().state, "unknown",
    "an End timeout must preserve truthful uncertainty");
assert.match(elements.testToneExecutionResult.textContent, /outcome is unknown/i,
    "an End timeout must not claim that the tone ended");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "End must remain available when the timed-out outcome may still be active");

first.close();
assert.equal(context.testToneLifecycleSnapshot().state, "unknown",
    "disconnect must preserve possible active-tone state");
assert.match(elements.testToneExecutionResult.textContent, /connection lost.*outcome is unknown/i,
    "disconnect must replace stale execution success with current uncertainty");
const second = context.openConnection();
assert.equal(buttons["#testToneEnd"].disabled, false,
    "End must remain available after reconnect even before catalog authorization");
assert.equal(buttons["#testToneStart"].disabled, true,
    "catalog loss must continue to gate Start independently");
second.message({ command: "tone_end", stopped: true });
assert.equal(context.testToneLifecycleSnapshot().state, "idle",
    "a recovery End response must settle the unknown state");
assert.equal(elements.testToneExecutionResult.textContent, "Test Tone transaction ended; restoration details unavailable.",
    "recovery End must report only the confirmed outcome");
});

runScenario("disconnect reconnect and catalog failure lifecycle", () => {
const first = openAuthorizedConnection();
selectBandForTimedStart("20m");
startSelectedToneAndRunRealTimeout(first, "wspr_band");

first.close();
assert.equal(context.catalogSnapshot().authorized, false, "disconnect revokes current authorization");
assertStartDisabled("last-valid catalog must not authorize Start after disconnect");
assert.match(context.testToneFrequencyContextText(), /catalog unavailable/i,
    "catalog loss remains visible once in the modal frequency context");
context.renderTestToneSelection();
assert.equal(elements.testToneSelectionError.textContent, "",
    "catalog loss must not be duplicated in the selection error region");
assert.equal(JSON.stringify(context.testToneStartSnapshot()), JSON.stringify({
    pending: false,
    source: "",
    hasUnresolvedContext: false,
    quarantined: false,
    timeoutHandle: null,
}), "disconnect must discard timed-out context and quarantine belonging to the old socket");
assert.equal(context.parseConfiguredWsprFrequencyHz("30m"), 10138700,
    "last-valid catalog remains available only for display/configuration continuity");

const second = context.openConnection();
assert.deepEqual(commandMessages(second, "wspr_band_catalog"), [{ command: "wspr_band_catalog" }],
    "reconnect requests a fresh catalog");
assert.equal(context.catalogSnapshot().authorized, false, "reconnect begins unauthorized");
first.message({ command: "tone_start", started: true });
assert.equal(buttons["#testToneEnd"].disabled, false,
    "a stale response must not remove recovery End availability from the replacement connection");
first.message(validCatalog(1500));
assert.equal(context.catalogSnapshot().authorized, false, "delayed old-connection response cannot authorize reconnect");
assertStartDisabled("stale response cannot enable Start");
const secondPending = context.catalogSnapshot();
second.message({ command: "wspr_band_catalog", status: "error" });
assert.equal(context.catalogSnapshot().authorized, false, "backend error remains unauthorized");
assert.equal(context.catalogSnapshot().pending, false, "backend error clears pending state");
assert.equal(timers.has(secondPending.timeoutHandle), false, "backend error clears the catalog timer");
assert.ok(clearedTimers.has(secondPending.timeoutHandle), "backend error cancels the timer directly");
assert.match(context.catalogSnapshot().message, /unavailable/i, "backend error is operator-visible");
assertStartDisabled("backend error keeps Start disabled");
second.close();

const third = context.openConnection();
assert.equal(commandMessages(third, "wspr_band_catalog").length, 1,
    "each new connection has its own catalog request");
const thirdPending = context.catalogSnapshot();
third.message({ command: "wspr_band_catalog", status: "ok", audio_offset_hz: 1500, bands: {} });
assert.equal(context.catalogSnapshot().authorized, false, "invalid response never authorizes Start");
assert.equal(context.catalogSnapshot().pending, false, "invalid response clears pending state");
assert.equal(timers.has(thirdPending.timeoutHandle), false, "invalid response clears the catalog timer");
assert.ok(clearedTimers.has(thirdPending.timeoutHandle), "invalid response cancels the timer directly");
assert.match(context.catalogSnapshot().message, /invalid/i, "invalid response is operator-visible");
third.close();

const fourth = context.openConnection();
const fourthPending = context.catalogSnapshot();
runTimer(fourthPending.timeoutHandle);
assert.equal(context.catalogSnapshot().authorized, false, "missing response never authorizes Start");
assert.equal(context.catalogSnapshot().pending, false, "timeout clears pending state");
assert.equal(timers.has(fourthPending.timeoutHandle), false, "timeout leaves no timer armed");
assert.ok(clearedTimers.has(fourthPending.timeoutHandle), "timeout cancellation is recorded directly");
assert.match(context.catalogSnapshot().message, /timed out/i, "catalog timeout is operator-visible");
assertStartDisabled("timeout keeps Start disabled");
fourth.close();

const fifth = context.openConnection();
const fifthPending = context.catalogSnapshot();
fifth.close();
assert.equal(context.catalogSnapshot().pending, false, "disconnect clears pending state");
assert.equal(timers.has(fifthPending.timeoutHandle), false, "disconnect clears the catalog timer");
assert.ok(clearedTimers.has(fifthPending.timeoutHandle), "disconnect cancels the timer directly");

const sixth = context.openConnection();
sixth.message(validCatalog(1500));
assert.equal(context.catalogSnapshot().authorized, true, "fresh valid response restores authorization");
assert.equal(bridge.inspect().catalog.authorized, true,
    "catalog state changed by production socket callbacks must be visible through bridge inspection");
assert.equal(buttons["#testToneStart"].disabled, true,
    "fresh catalog authorization cannot override an unresolved possible tone");
assert.equal(buttons["#testToneEnd"].disabled, false,
    "recovery End remains available after fresh catalog authorization");
assert.match(context.testToneFrequencyContextText(), /Configured frequency:/,
    "validated catalog restores normal frequency context");

sixth.close();
const sendFailureTimer = nextTimerId;
FakeWebSocket.throwOnNextSend = true;
const seventh = context.openConnection();
assert.equal(commandMessages(seventh, "wspr_band_catalog").length, 0,
    "throwing send does not emit a catalog request");
assert.equal(context.catalogSnapshot().pending, false, "send failure clears pending state");
assert.equal(context.catalogSnapshot().authorized, false, "send failure revokes authorization");
assert.equal(timers.has(sendFailureTimer), false, "send failure clears its timer");
assert.ok(clearedTimers.has(sendFailureTimer), "send failure cancels the timer directly");
assert.match(context.catalogSnapshot().message, /could not be sent/i, "send failure is operator-visible");
assertStartDisabled("send failure keeps Start disabled");
seventh.message(validCatalog(1500));
assert.equal(context.catalogSnapshot().authorized, false,
    "a response after send failure cannot authorize the failed request");
});

let staleScenarioTimerCallback = null;
let staleScenarioSocket = null;
let staleScenarioMessageCallback = null;
runScenario("isolation source state", () => {
    staleScenarioSocket = context.openConnection();
    const pendingCatalog = context.catalogSnapshot();
    staleScenarioTimerCallback = timerCallbacks.get(pendingCatalog.timeoutHandle)?.callback;
    staleScenarioMessageCallback = staleScenarioSocket.listeners.message?.[0];
    assert.equal(typeof staleScenarioTimerCallback, "function",
        "the source scenario must retain the exact raw production catalog timeout callback");
    assert.equal(typeof staleScenarioMessageCallback, "function",
        "the source scenario must retain the exact raw production socket message callback");
    elements.testToneExecutionResult.textContent = "scenario-local result";
    diagnostics.throwCount = 1;
    bridge.functions.showTestToneBlockedModal("active", "scenario-local modal");
    assert.ok(guardModalShowCount > 0, "the source scenario must exercise the modal mock");
});

function isolationSnapshot(currentScenarioSocket) {
    return JSON.stringify({
        catalog: bridge.inspect().catalog,
        testToneStart: bridge.inspect().testToneStart,
        executionResult: elements.testToneExecutionResult.textContent,
        startDisabled: elements.testToneStart.disabled,
        endDisabled: elements.testToneEnd.disabled,
        hasCurrentSocket: bridge.hasCurrentSocket(),
        currentSocketReadyState: bridge.currentSocketReadyState(),
        timers: [...timers.entries()].map(([id, record]) => ({
            id,
            delay: record.delay,
            generation: record.generation,
            kind: record.kind,
            state: record.state,
        })),
        intervals: [...intervals.entries()].map(([id, record]) => ({
            id,
            delay: record.delay,
            generation: record.generation,
            kind: record.kind,
            state: record.state,
        })),
        currentSocketMessages: currentScenarioSocket?.sent || [],
    });
}

runScenario("isolation target state", () => {
    const socket = openAuthorizedConnection();
    selectBandForTimedStart("20m");
    const beforeStaleCallbacks = isolationSnapshot(socket);
    let staleTimerCallbackInvoked = false;
    staleScenarioTimerCallback.call(undefined);
    staleTimerCallbackInvoked = true;
    assert.equal(staleTimerCallbackInvoked, true,
        "the retained raw production timeout callback must execute after reset");
    assert.equal(isolationSnapshot(socket), beforeStaleCallbacks,
        "the stale production timeout callback must not mutate the next scenario");

    bridge.setConsoleLogLevelForTest("debug");
    const diagnosticsBeforeStaleSocket = diagnostics.calls.length;
    let staleSocketCallbackInvoked = false;
    staleScenarioMessageCallback.call(staleScenarioSocket, {
        data: JSON.stringify(validCatalog(1500)),
    });
    staleSocketCallbackInvoked = true;
    assert.equal(staleSocketCallbackInvoked, true,
        "the retained raw production socket callback must execute after reset");
    assert.equal(diagnostics.calls.length, diagnosticsBeforeStaleSocket,
        "the stale production socket callback must exit before diagnostics or state changes");
    assert.equal(isolationSnapshot(socket), beforeStaleCallbacks,
        "the stale production socket callback must not mutate the next scenario");
    assert.equal(bridge.hasCurrentSocket(), true,
        "a stale socket callback must not detach the current scenario socket");
    assert.equal(bridge.currentSocketReadyState(), FakeWebSocket.OPEN,
        "a stale socket callback must not alter the current socket state");
    assert.equal(guardModalShowCount, 0, "Bootstrap modal records must reset per scenario");
    assert.equal(diagnostics.throwCount, 0, "diagnostic throw injection must reset per scenario");
    context.bindTestToneControls();
    assert.equal(handlerCount(elements.testToneStart, "click"), 1,
        "production rebinding must leave exactly one effective Start handler");
    dispatch(elements.testToneStart, "click", { preventDefault() {} });
    assert.equal(context.toneStartMessages(socket).length, 1,
        "one rebound Start click must produce one request without accumulated handlers");
});

runScenario("bridge reset deterministic defaults", () => {
assert.deepEqual(JSON.parse(JSON.stringify(bridge.inspect().catalog)), {
    authorized: false,
    pending: false,
    message: "WSPR band catalog unavailable. Test Tone Start is disabled.",
    offset: 0,
    dialFrequenciesHz: {},
    connectionGeneration: 0,
    requestGeneration: 0,
    pendingRequestGeneration: null,
    timeoutHandle: null,
}, "the narrow bridge reset must restore deterministic production catalog state between cases");
assert.equal(bridge.inspect().testToneStart.hasUnresolvedContext, false,
    "the narrow bridge reset must not retain production Start attribution state");
});

runScenario("versioned Test Tone outcomes preserve newer evidence and restoration", () => {
    const socket = openAuthorizedConnection();
    selectBandForTimedStart("20m");
    dispatch(elements.testToneStart, "click", { preventDefault() {} });
    const requestId = context.toneStartMessages(socket).at(-1).client_request_id;
    assert.ok(requestId);
    const status = (revision, state, owned = true, extra = {}) => ({
        generation: 7, revision, state, owned, message: "", restoration: "none", ...extra,
    });
    socket.message({ command: "tone_start", client_request_id: "another-client", started: false,
        test_tone: status(10, "starting") });
    assert.equal(context.testToneStartSnapshot().pending, true,
        "another client's rejection must not consume the pending request");
    const accepted = { command: "tone_start", client_request_id: requestId, started: true,
        frequency_source: "wspr_band", band: "20m", dial_frequency_hz: 14095600,
        audio_offset_hz: 2750, actual_rf_frequency_hz: 14098350,
        selector_gpio_enabled: false, test_tone: status(11, "starting") };
    socket.message(accepted);
    assert.equal(context.testToneLifecycleSnapshot().state, "starting");
    assert.match(elements.testToneExecutionResult.textContent, /waiting for runtime confirmation/);
    assert.doesNotMatch(elements.testToneExecutionResult.textContent, /14\.098350/,
        "matched execution status must not repeat the preview frequency");
    socket.message({ command: "test_tone_status", test_tone: status(12, "active") });
    assert.equal(context.testToneLifecycleSnapshot().state, "active");
    const activeText = elements.testToneExecutionResult.textContent;
    socket.message(accepted);
    socket.message({ command: "tone_start", started: false, message: "old rejection" });
    assert.equal(elements.testToneExecutionResult.textContent, activeText);
    socket.message({ command: "tone_start", ...accepted, test_tone: status(12, "active") });
    assert.equal(elements.testToneExecutionResult.textContent, activeText,
        "an equal-revision duplicate cannot erase committed details");
    dispatch(elements.testToneEnd, "click", { preventDefault() {} });
    const endId = commandMessages(socket, "tone_end").at(-1).client_request_id;
    socket.message({ command: "test_tone_status", test_tone: status(13, "active") });
    socket.message({ ...accepted, test_tone: status(13, "active") });
    assert.equal(context.testToneLifecycleSnapshot().state, "end_pending");
    assert.equal(elements.testToneEnd.disabled, true,
        "a runtime observation cannot enable a duplicate pending End");
    dispatch(elements.testToneEnd, "click", { preventDefault() {} });
    assert.equal(commandMessages(socket, "tone_end").length, 1);
    socket.message({ command: "tone_end", client_request_id: endId, stopped: true,
        message: "Test Tone ended; previous CLI direct tone restored (startup requested).",
        test_tone: status(14, "ended", false, { restoration: "direct_tone" }) });
    assert.match(elements.testToneExecutionResult.textContent, /CLI direct tone restored/);
    assert.doesNotMatch(elements.testToneExecutionResult.textContent, /output.*off/i);
    socket.message(accepted);
    assert.equal(context.testToneLifecycleSnapshot().state, "idle");
    socket.message({ command: "tone_end", stopped: true, restoration_failed: true, status: "error",
        message: "Restoration failed; use local recovery.",
        test_tone: status(15, "unknown", false, { message: "Restoration failed; use local recovery.", restoration: "failed" }) });
    assert.equal(context.testToneLifecycleSnapshot().state, "unknown");
    assert.match(elements.testToneExecutionResult.textContent, /Restoration failed/);
    assert.equal(elements.testToneEnd.disabled, false);
    socket.message({ command: "test_tone_status", test_tone: status(16, "failed", false,
        { message: "Startup failed; output cleanup confirmed." }) });
    assert.equal(context.testToneLifecycleSnapshot().state, "idle");
    assert.match(elements.testToneExecutionResult.textContent, /Startup failed/);
});

runScenario("runtime observation can precede the correlated acknowledgment", () => {
    const socket = openAuthorizedConnection();
    selectBandForTimedStart("20m");
    dispatch(elements.testToneStart, "click", { preventDefault() {} });
    const start = context.toneStartMessages(socket).at(-1);
    const status = (revision, state, owned = true) => ({ generation: 22, revision, state, owned,
        message: state === "ended" ? "Test Tone ended; scheduler configuration restored." : "", restoration: "scheduler" });
    socket.message({ command: "test_tone_status", test_tone: status(32, "active") });
    assert.equal(context.testToneStartSnapshot().pending, true);
    socket.message({ command: "tone_start", client_request_id: start.client_request_id, started: true,
        frequency_source: "wspr_band", band: "20m", dial_frequency_hz: 14095600,
        audio_offset_hz: 2750, actual_rf_frequency_hz: 14098350, selector_gpio_enabled: false,
        test_tone: status(31, "starting") });
    assert.equal(context.testToneStartSnapshot().pending, false);
    assert.equal(context.testToneLifecycleSnapshot().state, "active");
    assert.match(elements.testToneExecutionResult.textContent, /Requested frequency committed/);
    dispatch(elements.testToneEnd, "click", { preventDefault() {} });
    const end = commandMessages(socket, "tone_end").at(-1);
    socket.message({ command: "test_tone_status", test_tone: status(34, "ended", false) });
    socket.message({ command: "tone_end", client_request_id: end.client_request_id, stopped: true,
        test_tone: status(33, "ended", false) });
    assert.equal(context.testToneLifecycleSnapshot().endTimeoutHandle, null);
    assert.equal(context.testToneLifecycleSnapshot().state, "idle");
});

console.log("wspr_band_frequency_correlation_test passed");
