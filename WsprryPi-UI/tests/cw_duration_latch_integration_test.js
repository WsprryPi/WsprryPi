"use strict";

const assert = require("node:assert/strict");
const http = require("node:http");
const net = require("node:net");
const path = require("node:path");
const fs = require("node:fs");
const { spawn } = require("node:child_process");
const WebSocket = require("ws");

const UI_ROOT = path.resolve(__dirname, "..");

function freePort() {
    return new Promise((resolve, reject) => {
        const server = net.createServer();
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            const { port } = server.address();
            server.close((error) => error ? reject(error) : resolve(port));
        });
    });
}

function getJson(url) {
    return new Promise((resolve, reject) => {
        http.get(url, (response) => {
            let body = "";
            response.setEncoding("utf8");
            response.on("data", (chunk) => { body += chunk; });
            response.on("end", () => {
                try {
                    resolve(JSON.parse(body));
                } catch (error) {
                    reject(error);
                }
            });
        }).on("error", reject);
    });
}

function getStatus(url) {
    return new Promise((resolve, reject) => {
        http.get(url, (response) => {
            response.resume();
            response.on("end", () => resolve(response.statusCode));
        }).on("error", reject);
    });
}

async function waitFor(check, description, timeoutMs = 10000) {
    const deadline = Date.now() + timeoutMs;
    let lastError;
    while (Date.now() < deadline) {
        try {
            const value = await check();
            if (value) return value;
        } catch (error) {
            lastError = error;
        }
        await new Promise((resolve) => setTimeout(resolve, 50));
    }
    throw new Error(`Timed out waiting for ${description}${lastError ? `: ${lastError.message}` : ""}`);
}

class CdpClient {
    constructor(url) {
        this.socket = new WebSocket(url);
        this.nextId = 1;
        this.pending = new Map();
        this.socket.on("message", (raw) => {
            const message = JSON.parse(raw);
            if (!message.id || !this.pending.has(message.id)) return;
            const { resolve, reject } = this.pending.get(message.id);
            this.pending.delete(message.id);
            if (message.error) reject(new Error(message.error.message));
            else resolve(message.result);
        });
    }

    async open() {
        if (this.socket.readyState === WebSocket.OPEN) return;
        await new Promise((resolve, reject) => {
            this.socket.once("open", resolve);
            this.socket.once("error", reject);
        });
    }

    send(method, params = {}) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pending.set(id, { resolve, reject });
            this.socket.send(JSON.stringify({ id, method, params }));
        });
    }

    close() {
        this.socket.close();
    }
}

async function browserTest() {
    const fail = (message) => { throw new Error(message); };
    const equal = (actual, expected, message) => {
        if (actual !== expected) {
            fail(`${message}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
        }
    };
    const ok = (condition, message) => { if (!condition) fail(message); };
    const includes = (actual, expected, message) => {
        ok(String(actual).includes(expected), `${message}: ${JSON.stringify(actual)}`);
    };

    class FakeClock {
        constructor() {
            this.now = 0;
            this.nextId = 1;
            this.tasks = new Map();
        }

        setTimeout(callback, delay = 0) {
            const id = this.nextId++;
            this.tasks.set(id, { callback, due: this.now + Number(delay || 0) });
            return id;
        }

        clearTimeout(id) {
            this.tasks.delete(id);
        }

        tick(milliseconds) {
            const end = this.now + milliseconds;
            while (true) {
                const ready = [...this.tasks.entries()]
                    .filter(([, task]) => task.due <= end)
                    .sort((a, b) => a[1].due - b[1].due || a[0] - b[0])[0];
                if (!ready) break;
                const [id, task] = ready;
                this.tasks.delete(id);
                this.now = task.due;
                task.callback();
            }
            this.now = end;
        }

        reset() {
            this.now = 0;
            this.tasks.clear();
        }
    }

    class FakeDeferred {
        constructor() {
            this.doneCallbacks = [];
            this.failCallbacks = [];
            this.alwaysCallbacks = [];
            this.settled = false;
            this.outcome = "";
            this.args = [];
        }

        done(callback) {
            if (this.outcome === "resolved") callback(...this.args);
            else if (!this.settled) this.doneCallbacks.push(callback);
            return this;
        }
        fail(callback) {
            if (this.outcome === "rejected") callback(...this.args);
            else if (!this.settled) this.failCallbacks.push(callback);
            return this;
        }
        always(callback) {
            if (this.settled) callback(...this.args);
            else this.alwaysCallbacks.push(callback);
            return this;
        }

        resolve() {
            if (this.settled) return;
            this.settled = true;
            this.outcome = "resolved";
            this.doneCallbacks.forEach((callback) => callback());
            this.alwaysCallbacks.forEach((callback) => callback());
        }

        reject(xhr, textStatus = "error") {
            if (this.settled) return;
            this.settled = true;
            this.outcome = "rejected";
            this.args = [xhr, textStatus];
            this.failCallbacks.forEach((callback) => callback(xhr, textStatus));
            this.alwaysCallbacks.forEach((callback) => callback());
        }
    }

    const clock = new FakeClock();
    window.setTimeout = clock.setTimeout.bind(clock);
    window.clearTimeout = clock.clearTimeout.bind(clock);

    const patches = [];
    let patchMode = "success";
    let pendingPatch = null;
    ajaxWithEndpointFallback = (endpoint, options) => {
        const deferred = new FakeDeferred();
        patches.push({ endpoint, options, deferred });
        if (patchMode === "success") deferred.resolve();
        else if (patchMode === "pending") pendingPatch = deferred;
        else if (patchMode === "transient") deferred.reject({ status: 0 }, "error");
        return deferred;
    };

    const statusTransitions = [];
    const realSetConfigSaveStatus = setConfigSaveStatus;
    setConfigSaveStatus = (state, message = "", detail = "", options = {}) => {
        statusTransitions.push({ state, message, detail });
        return realSetConfigSaveStatus(state, message, detail, options);
    };

    const dialogs = [];
    showMessageDialog = (options) => { dialogs.push(options); };
    showBackendStatus = () => {};
    clearBackendStatus = () => {};
    persistLocalConfigDraftIfPossible = () => {};
    removePersistedConfigDraft = () => {};

    const field = (id) => document.getElementById(id);
    const setValue = (id, value, eventType = "input") => {
        field(id).value = String(value);
        field(id).dispatchEvent(new Event(eventType, { bubbles: true }));
    };
    const selectMode = (mode) => {
        field("qrss_mode").checked = true;
        ["QRSS", "FSKCW", "DFCW"].forEach((value) => {
            field(`mode_${value.toLowerCase()}`).checked = value === mode;
        });
    };

    // Keep this focused on the real CW controls and their validation. Other
    // Setup panels are orthogonal and may be empty when the PHP fixture runs
    // without a controller backend.
    validatePage = () => {
        const mode = selectedConfigMode();
        let valid = validateCwMessage() &&
            validateCwDotSeconds() &&
            validateCwRepeatMinutes() &&
            validateCwStartSecond();
        const activeIds = mode === "DFCW"
            ? ["dfcw_intra_element_gap", "dfcw_inter_character_gap", "dfcw_inter_word_gap"]
            : ["cw_intra_element_gap", "cw_inter_character_gap", "cw_inter_word_gap"];
        activeIds.forEach((id) => {
            valid = validatePositiveCwField(id, "Enter a positive spacing value.") && valid;
        });
        return valid;
    };

    const reset = (mode = "QRSS") => {
        clock.reset();
        patches.length = 0;
        dialogs.length = 0;
        statusTransitions.length = 0;
        patchMode = "success";
        pendingPatch = null;
        configAutosaveTimer = null;
        configSaveStatusClearTimer = null;
        configAutosaveSuspended = false;
        configAutosaveInFlight = false;
        configAutosavePendingAfterFlight = false;
        configAutosaveDirty = false;
        configAutosaveNetworkPaused = false;
        lastSavedConfigPayload = "";
        persistedStationIdentity = null;
        lastFailedConfigPayload = "";
        lastFailedConfigMessage = "";
        cwDurationPolicyLatched = false;
        selectMode(mode);
        field("qrss_message").value = "E";
        field("dot_length").value = "1";
        field("tx_repeat_every").value = "1";
        field("tx_start_second").value = "5";
        field("cw_intra_element_gap").value = "1";
        field("cw_inter_character_gap").value = "3";
        field("cw_inter_word_gap").value = "7";
        field("dfcw_intra_element_gap").value = "0.333333";
        field("dfcw_inter_character_gap").value = "1";
        field("dfcw_inter_word_gap").value = "3";
        field("qrss_message").setCustomValidity("");
        clearFieldValidationState(field("qrss_message"));
        realSetConfigSaveStatus("", "", "");
        statusTransitions.length = 0;
    };

    const assertLatched = (draft) => {
        equal(field("qrss_message").value, draft, "draft must remain unchanged");
        includes(field("qrss_message").validationMessage, "calculated", "custom validity must explain duration");
        includes(field("qrss_message").validationMessage, "repeat interval", "custom validity must name repeat interval");
        ok(field("qrss_message").classList.contains("is-invalid"), "Message must have is-invalid");
        equal(field("qrss_message").getAttribute("aria-invalid"), "true", "Message aria-invalid");
        equal(field("configSaveStatus").textContent, "Save failed", "inline short status");
        includes(field("configSaveStatusDetail").textContent, "calculated", "inline detail must explain duration");
    };

    // Pico limits are independent of the repeat interval and preserve the draft.
    const realWtpUi = window.WtpUi;
    window.WtpUi = { selected: () => true, maximumJobDurationSeconds: 3600, maximumJobEvents: 512 };
    for (const mode of ["QRSS", "FSKCW", "DFCW"]) {
        reset(mode); field("tx_repeat_every").value = "120";
        for (const count of [31, 32]) {
            field("qrss_message").value = "?".repeat(count);
            ok(validateCwMessage(), mode + " must accept " + count + " complex characters");
        }
        field("qrss_message").value = "?".repeat(33);
        ok(!validateCwMessage(), mode + " must reject 33 characters");
        equal(field("qrss_message").value.length, 33, "Pico validation must not truncate");
        includes(field("qrss_message").validationMessage, "32 characters including spaces", "length explanation");
        field("qrss_message").value = " ".repeat(31) + "E";
        ok(validateCwMessage(), "spaces count within 32");
        field("qrss_message").value = " ".repeat(32) + "E";
        ok(!validateCwMessage(), "spaces count at 33");
        field("qrss_message").value = "E";
        for (const dot of [3599.999999999, 3600]) {
            field("dot_length").value = String(dot);
            ok(validateCwMessage(), "Pico duration accepts boundary " + dot);
        }
        field("dot_length").value = "3600.000001";
        ok(!validateCwMessage(), "Pico duration above 60 minutes rejected");
        includes(field("qrss_message").validationMessage, "This Pico allows", "duration names actual device limit");
        field("dot_length").value = "0.1";field("qrss_message").value = "?".repeat(32);
        window.WtpUi.maximumJobEvents = 162;
        ok(!validateCwMessage(), "old event CAPS reject complex message");
        includes(field("qrss_message").validationMessage, "383 events", "event explanation is distinct");
        window.WtpUi.maximumJobEvents = 512;
        window.WtpUi.maximumJobDurationSeconds = 110.592;
        field("dot_length").value = "111";field("qrss_message").value = "E";
        ok(!validateCwMessage(), "old duration CAPS remain authoritative");
        window.WtpUi.maximumJobDurationSeconds = 3600;
    }
    reset();field("tx_repeat_every").value="120";
    field("dot_length").value="0.3333333339";field("qrss_message").value="T";
    updateCwMessageLengthEstimate();
    includes(field("cw_message_length_estimate").textContent,"0.999999999 s","quantize dot before multiplying dash");
    includes(field("cw_message_length_estimate").textContent,"1 / 512 events","normal preview names event ceiling");
    field("dot_length").value="3600.000000001";field("qrss_message").value="E";
    ok(!validateCwMessage(),"one ns above duration rejected");
    includes(field("qrss_message").validationMessage,"3600.000000001 s","rejection preserves exact calculated duration");
    window.WtpUi = { selected: () => false };
    reset();field("tx_repeat_every").value="120";field("qrss_message").value="E".repeat(33);
    ok(validateCwMessage(), "Pico character limit must not affect another backend");
    window.WtpUi = realWtpUi;

    // 1 and 2: local overlong input and repeated overlong edits.
    reset();
    field("dot_length").value = "20";
    setValue("qrss_message", "EE");
    clock.tick(800);
    assertLatched("EE");
    equal(patches.length, 0, "overlong local edit must not PATCH");
    setValue("qrss_message", "EEE");
    clock.tick(800);
    assertLatched("EEE");
    equal(patches.length, 0, "repeated overlong edit must not PATCH");
    equal(dialogs.length, 0, "repeated overlong edit must not open modal");

    // 3: shortening EE to T produces exactly 60 seconds at a 20-second dot.
    statusTransitions.length = 0;
    setValue("qrss_message", "T");
    clock.tick(800);
    equal(field("qrss_message").validationMessage, "", "equality must clear duration validity");
    ok(!field("qrss_message").classList.contains("is-invalid"), "equality must clear invalid styling");
    equal(field("qrss_message").getAttribute("aria-invalid"), "false", "equality aria-invalid");
    equal(patches.length, 1, "shortening recovery must PATCH once");
    ok(statusTransitions.some(({ state }) => state === "saving"), "recovery must enter Saving");
    ok(statusTransitions.some(({ state }) => state === "saved"), "recovery must enter Saved");

    // 4: increasing repeat interval clears the latch and saves once.
    reset();
    field("dot_length").value = "61";
    setValue("qrss_message", "E");
    assertLatched("E");
    setValue("tx_repeat_every", "2");
    clock.tick(800);
    equal(patches.length, 1, "repeat interval recovery must PATCH once");
    equal(field("qrss_message").validationMessage, "", "repeat recovery validity");
    equal(field("qrss_message").getAttribute("aria-invalid"), "false", "repeat recovery aria-invalid");

    // 5: dot length and only the active spacing group control recovery.
    reset();
    field("dot_length").value = "61";
    setValue("qrss_message", "E");
    setValue("dot_length", "60");
    clock.tick(800);
    equal(patches.length, 1, "dot-length recovery must PATCH once");
    equal(field("qrss_message").validationMessage, "", "dot-length recovery validity");

    reset("QRSS");
    field("dot_length").value = "20";
    setValue("qrss_message", "EE");
    setValue("dfcw_inter_character_gap", "0.1");
    clock.tick(800);
    assertLatched("EE");
    equal(patches.length, 0, "inactive DFCW spacing must not recover QRSS");
    setValue("cw_inter_character_gap", "0.5");
    clock.tick(800);
    equal(patches.length, 1, "active QRSS spacing recovery must PATCH once");
    equal(field("qrss_message").validationMessage, "", "active spacing recovery validity");

    // 6: ordinary validation wins after duration-latch clearing.
    reset();
    field("dot_length").value = "61";
    setValue("qrss_message", "E");
    setValue("qrss_message", "");
    clock.tick(800);
    equal(field("qrss_message").validationMessage, "CW message is required.", "empty validation must survive");
    ok(field("qrss_message").classList.contains("is-invalid"), "empty Message remains invalid");
    equal(patches.length, 0, "empty Message must not PATCH");

    reset();
    field("dot_length").value = "61";
    setValue("qrss_message", "E");
    setValue("qrss_message", "E@");
    clock.tick(800);
    equal(field("qrss_message").validationMessage, "CW message contains unsupported character @.", "unsupported validation must survive");
    ok(field("qrss_message").classList.contains("is-invalid"), "unsupported Message remains invalid");
    equal(patches.length, 0, "unsupported Message must not PATCH");

    // 7: a structured rejection latches inline and completion cannot retry.
    reset();
    patchMode = "pending";
    setValue("qrss_message", "E");
    clock.tick(800);
    equal(patches.length, 1, "valid draft must begin one PATCH");
    field("dot_length").value = "61";
    validateCwMessage();
    pendingPatch.reject({
        status: 400,
        responseJSON: {
            error: "invalid_config",
            message: "Configured QRSS message duration of 1m 01s exceeds repeat_every interval of 1m 00s. Reduce the message length, shorten the unit length, or increase repeat_every.",
            policy: "cw_duration_repeat_interval",
            field: "CW.Message",
            mode: "QRSS",
            message_duration_seconds: 61,
            repeat_interval_seconds: 60,
        },
    });
    assertLatched("E");
    clock.tick(1600);
    equal(patches.length, 1, "structured rejection completion must not retry overlong payload");

    // 8: exercise the real connectWebSocket message routing.
    class FakeWebSocket {
        static CONNECTING = 0;
        static OPEN = 1;
        static CLOSED = 3;
        constructor(url) {
            this.url = url;
            this.readyState = FakeWebSocket.CONNECTING;
            this.listeners = new Map();
            FakeWebSocket.instances.push(this);
        }
        addEventListener(type, callback) {
            if (!this.listeners.has(type)) this.listeners.set(type, []);
            this.listeners.get(type).push(callback);
        }
        emit(type, event) {
            (this.listeners.get(type) || []).forEach((callback) => callback(event));
        }
        close() { this.readyState = FakeWebSocket.CLOSED; }
    }
    FakeWebSocket.instances = [];
    if (ws && typeof ws.close === "function") ws.close();
    ws = null;
    window.WebSocket = FakeWebSocket;
    connectWebSocket({ proxyUrl: "ws://test/socket", directUrl: "ws://test:8081", name: "test" }, 5000);
    const socket = FakeWebSocket.instances[0];
    ok(socket, "fake WebSocket must be created");

    reset();
    field("dot_length").value = "61";
    setValue("qrss_message", "E");
    const durationMessage = "Configured QRSS message duration of 1m 01s exceeds repeat_every interval of 1m 00s. Reduce the message length, shorten the unit length, or increase repeat_every.";
    socket.emit("message", { data: JSON.stringify({
        type: "configuration",
        state: "reload_failed",
        message: durationMessage,
    }) });
    assertLatched("E");
    equal(dialogs.length, 0, "mapped duration reload failure must not open modal");

    field("dot_length").value = "1";
    validateCwMessage();
    socket.emit("message", { data: JSON.stringify({
        type: "configuration",
        state: "reload_failed",
        message: durationMessage,
    }) });
    equal(dialogs.length, 1, "duration message with valid draft must use generic modal");
    equal(dialogs[0].title, "Configuration Reload Failed", "duration fallback modal title");

    socket.emit("message", { data: JSON.stringify({
        type: "configuration",
        state: "reload_failed",
        message: "Unrelated backend failure.",
    }) });
    equal(dialogs.length, 2, "unrelated reload failure must open generic modal");
    equal(dialogs[1].message, "Unrelated backend failure.", "unrelated fallback message");

    // 9: start-second drafts validate locally, preserve zero, and autosave after correction.
    reset("DFCW");
    equal(field("tx_start_second").type, "number", "start second input type");
    equal(field("tx_start_second").min, "0", "start second minimum");
    equal(field("tx_start_second").max, "59", "start second maximum");
    equal(field("tx_start_second").step, "1", "start second step");
    ok(field("tx_start_second").required, "start second must be required");
    equal(
        getConfigIntValue({}, "CW", "Start Second", 5),
        5,
        "missing start second must populate with fallback 5"
    );
    equal(
        getConfigIntValue({ "Start Second": 0 }, "CW", "Start Second", 5),
        0,
        "population helper must preserve explicit zero"
    );
    ["QRSS", "FSKCW", "DFCW"].forEach((mode) => {
        selectMode(mode);
        field("tx_start_second").value = "59";
        ok(validateCwStartSecond(), `${mode} must share valid start-second behavior`);
    });
    const durationBefore = field("cw_message_length_estimate").textContent;
    setValue("tx_start_second", "5.5");
    clock.tick(800);
    equal(patches.length, 0, "fractional start second must block autosave");
    equal(field("tx_start_second").value, "5.5", "invalid start-second draft must remain local");
    equal(field("tx_start_second").getAttribute("aria-invalid"), "true", "invalid start second aria state");
    setValue("tx_start_second", "0");
    clock.tick(800);
    equal(patches.length, 1, "valid start-second correction must resume autosave");
    const savedPayload = JSON.parse(patches[0].options.data);
    equal(savedPayload.CW["Start Second"], 0, "autosave payload must preserve explicit zero");
    equal(field("cw_message_length_estimate").textContent, durationBefore, "start second must not alter duration estimate");
    field("wspr_mode").checked = true;
    field("qrss_mode").checked = false;
    field("tx_start_second").value = "invalid";
    ok(validateCwStartSecond(), "WSPR must ignore CW start-second validation");
    ok(!field("tx_start_second").classList.contains("is-invalid"), "WSPR must clear start-second invalid styling");

    // 10: a new edit immediately revokes stale Saved, then progresses normally.
    reset();
    syncConfigAutosaveBaseline();
    equal(field("configSaveStatus").textContent, "Saved", "baseline must begin saved");
    setValue("qrss_message", "T");
    equal(field("configSaveStatus").textContent, "Changes pending", "edit must immediately revoke Saved");
    equal(field("configSaveStatus").dataset.state, "pending", "pending status state");
    ok(hasUnsavedLocalConfigChanges(), "debounced edit must be unsaved");
    clock.tick(800);
    ok(statusTransitions.some(({ message }) => message === "Saving..."), "request must enter Saving");
    equal(field("configSaveStatus").textContent, "Saved", "successful current payload must show Saved");
    equal(hasUnsavedLocalConfigChanges(), false, "successful current payload must be saved");

    // 11: completion of an older in-flight payload cannot claim the newer edit is saved.
    reset();
    syncConfigAutosaveBaseline();
    patchMode = "pending";
    setValue("qrss_message", "T");
    clock.tick(800);
    equal(patches.length, 1, "first payload must begin one PATCH");
    equal(field("configSaveStatus").textContent, "Saving...", "first payload must show Saving");
    const firstPatch = pendingPatch;
    setValue("qrss_message", "EE");
    equal(field("configSaveStatus").textContent, "Changes pending", "newer edit must replace Saving with pending");
    ok(hasUnsavedLocalConfigChanges(), "newer edit must remain unsaved while first request is in flight");
    clock.tick(800);
    equal(patches.length, 1, "newer payload must queue behind first request");
    statusTransitions.length = 0;
    patchMode = "success";
    firstPatch.resolve();
    equal(field("configSaveStatus").textContent, "Changes pending", "older completion must retain pending status");
    ok(!statusTransitions.some(({ state }) => state === "saved"), "older completion must not enter Saved");
    ok(hasUnsavedLocalConfigChanges(), "older completion must not clear unsaved state");
    clock.tick(800);
    equal(patches.length, 2, "newer payload must subsequently PATCH");
    equal(field("configSaveStatus").textContent, "Saved", "only newest completion may show Saved");
    equal(hasUnsavedLocalConfigChanges(), false, "newest completion must clear unsaved state");

    // 12: invalid edits revoke stale Saved, then retain actionable validation feedback.
    reset();
    syncConfigAutosaveBaseline();
    setValue("qrss_message", "");
    equal(field("configSaveStatus").textContent, "Changes pending", "invalid edit must immediately revoke Saved");
    clock.tick(800);
    equal(field("configSaveStatus").textContent, "Invalid - not saved", "validation must replace pending feedback");
    equal(field("configSaveStatus").dataset.state, "invalid", "invalid status state");
    ok(hasUnsavedLocalConfigChanges(), "invalid edit must remain unsaved");

    // 13: suspended initial population must not display pending feedback.
    reset();
    syncConfigAutosaveBaseline();
    statusTransitions.length = 0;
    suspendConfigAutosave(true);
    setValue("qrss_message", "T");
    equal(field("configSaveStatus").textContent, "Saved", "suspended population must preserve baseline status");
    ok(!statusTransitions.some(({ state }) => state === "pending"), "suspended population must not enter pending");
    suspendConfigAutosave(false);

    // 14: invalid station identity remains local while an unrelated valid
    // setting is persisted with explicit partial-save feedback.
    reset();
    const focusedValidatePage = validatePage;
    validatePage = (options = {}) => options.allowInvalidStationIdentity === true;
    field("wspr_mode").checked = true;
    field("qrss_mode").checked = false;
    field("callsign").value = "NXXX";
    field("gridsquare").value = "ZZ99";
    persistedStationIdentity = { callsign: "NXXX", gridsquare: "ZZ99" };
    lastSavedConfigPayload = JSON.stringify(buildConfigPayload());
    setValue("callsign", "BAD CALL");
    field("use_led").checked = !field("use_led").checked;
    field("use_led").dispatchEvent(new Event("change", { bubbles: true }));
    clock.tick(800);
    equal(patches.length, 1, "unrelated valid change must PATCH despite invalid identity draft");
    const partialSavePayload = JSON.parse(patches[0].options.data);
    equal(partialSavePayload.WSPR["Call Sign"], "NXXX", "PATCH must retain persisted callsign");
    equal(partialSavePayload.WSPR["Grid Square"], "ZZ99", "PATCH must retain persisted locator");
    equal(
        partialSavePayload.Operation["Use LED"],
        field("use_led").checked,
        "PATCH must include the unrelated LED change"
    );
    equal(field("configSaveStatus").textContent, "Other changes saved", "partial-save status");
    includes(
        field("configSaveStatusDetail").textContent,
        "not valid for transmission",
        "partial-save detail must preserve the transmission safety boundary"
    );
    ok(hasUnsavedLocalConfigChanges(), "invalid identity draft must remain visibly unsaved");
    validatePage = focusedValidatePage;

    // 15: a transient controller failure pauses autosave without an 800 ms
    // request/status loop. Further edits stay local until an existing
    // connectivity signal resumes exactly one save of the newest payload.
    reset();
    syncConfigAutosaveBaseline();
    patchMode = "transient";
    setValue("qrss_message", "T");
    clock.tick(800);
    equal(patches.length, 1, "transient failure must attempt one PATCH");
    equal(field("configSaveStatus").textContent, "Save paused", "transient status must remain paused");
    includes(
        field("configSaveStatusDetail").textContent,
        "controller could not be reached",
        "transient detail must explain the controller failure"
    );
    ok(configAutosaveNetworkPaused, "transient failure must latch network pause");
    ok(hasUnsavedLocalConfigChanges(), "transient failure must retain unsaved state");
    const pausedTransitionCount = statusTransitions.length;
    setConfigLoadFailureState();
    equal(field("configSaveStatus").textContent, "Save paused", "config polling must preserve paused-save feedback");
    equal(statusTransitions.length, pausedTransitionCount, "config polling must not repeat paused live feedback");
    clock.tick(8000);
    equal(patches.length, 1, "paused autosave must not poll with PATCH requests");
    equal(statusTransitions.length, pausedTransitionCount, "paused autosave must not repeat live status announcements");

    setValue("qrss_message", "EE");
    clock.tick(8000);
    equal(patches.length, 1, "edits while paused must remain local");
    equal(field("configSaveStatus").textContent, "Save paused", "paused edit must not replace failure feedback");

    patchMode = "success";
    ok(resumeConfigAutosaveAfterControllerRecovery(), "controller recovery must resume a paused save");
    equal(field("configSaveStatus").textContent, "Connection restored", "recovery must be announced once");
    ok(!resumeConfigAutosaveAfterControllerRecovery(), "duplicate recovery signals must not queue another save");
    clock.tick(800);
    equal(patches.length, 2, "recovery must retry exactly once");
    equal(JSON.parse(patches[1].options.data).CW.Message, "EE", "recovery must save the newest draft");
    equal(field("configSaveStatus").textContent, "Saved", "successful recovery must settle as Saved");
    equal(hasUnsavedLocalConfigChanges(), false, "successful recovery must clear unsaved state");

    // 16: a successful config reload may prove that an uncertain PATCH was
    // already applied. Recovery must still clear the pause latch so the next
    // ordinary edit can save.
    reset();
    configAutosaveNetworkPaused = true;
    configAutosaveDirty = false;
    ok(!resumeConfigAutosaveAfterControllerRecovery(), "already-applied recovery needs no duplicate PATCH");
    ok(!configAutosaveNetworkPaused, "already-applied recovery must clear the pause latch");
    setValue("qrss_message", "T");
    clock.tick(800);
    equal(patches.length, 1, "the next edit after already-applied recovery must save normally");

    return {
        scenarios: 16,
        assertions: "passed",
        finalPatchCount: patches.length,
        finalDialogCount: dialogs.length,
    };
}

async function main() {
    const phpPort = await freePort();
    const debugPort = await freePort();
    const php = spawn("php", ["-S", `127.0.0.1:${phpPort}`, "-t", "data"], {
        cwd: UI_ROOT,
        stdio: "ignore",
    });
    const profileDir = `/tmp/wsprrypi-cw-duration-test-${process.pid}`;
    let chromium;

    let client;
    try {
        await waitFor(
            async () => await getStatus(
                `http://127.0.0.1:${phpPort}/index.php?page=config`
            ) === 200,
            "local PHP fixture"
        );
        const chromiumExecutable = process.env.CHROME_BIN || (
            process.platform === "darwin"
                ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
                : "chromium"
        );
        chromium = spawn(chromiumExecutable, [
            "--headless",
            "--no-sandbox",
            "--disable-gpu",
            `--remote-debugging-port=${debugPort}`,
            `--user-data-dir=${profileDir}`,
            `http://127.0.0.1:${phpPort}/index.php?page=config`,
        ], { stdio: "ignore" });

        const page = await waitFor(async () => {
            const pages = await getJson(`http://127.0.0.1:${debugPort}/json`);
            return pages.find((item) => item.type === "page");
        }, "Setup page in Chromium");

        client = new CdpClient(page.webSocketDebuggerUrl);
        await client.open();
        await client.send("Page.navigate", {
            url: `http://127.0.0.1:${phpPort}/index.php?page=config`,
        });
        await waitFor(async () => {
            const result = await client.send("Runtime.evaluate", {
                expression: "({ ready: typeof scheduleAutosave === 'function' && document.readyState === 'complete', state: document.readyState, title: document.title, url: location.href, scheduleType: typeof scheduleAutosave, scripts: [...document.scripts].map((script) => script.src) })",
                returnByValue: true,
            });
            if (!result.result.value.ready) {
                throw new Error(JSON.stringify(result.result.value));
            }
            return true;
        }, "Setup production scripts");

        const result = await client.send("Runtime.evaluate", {
            expression: `(${browserTest.toString()})()`,
            awaitPromise: true,
            returnByValue: true,
        });
        if (result.exceptionDetails) {
            const detail = result.exceptionDetails.exception &&
                result.exceptionDetails.exception.description;
            throw new Error(detail || result.exceptionDetails.text || "Browser test failed");
        }
        assert.deepEqual(result.result.value, {
            scenarios: 16,
            assertions: "passed",
            finalPatchCount: 1,
            finalDialogCount: 0,
        });

        for (const viewport of [
            { name: "desktop", width: 1280, height: 900 },
            { name: "mobile", width: 375, height: 812 },
        ]) {
            await client.send("Emulation.setDeviceMetricsOverride", {
                width: viewport.width,
                height: viewport.height,
                deviceScaleFactor: 1,
                mobile: viewport.name === "mobile",
            });
            const layout = await client.send("Runtime.evaluate", {
                expression: `(() => {
                    const states = [
                        ["saved", "Saved", "", {}],
                        ["saving", "Saving...", "", {}],
                        ["error", "Save paused", "The controller could not be reached for this save. Changes stay local until retry.", {}],
                        ["warning", "Identity change not saved", "The station identity remains local and is not valid for transmission. Fix Call Sign and Grid locator before transmitting.", {
                            detailActionLabel: "Review station identity",
                            onDetailAction: () => {},
                        }],
                    ];
                    return states.map(([state, message, detail, options]) => {
                        setConfigSaveStatus(state, message, detail, options);
                        const detailNode = document.getElementById("configSaveStatusDetail");
                        const hintNode = document.getElementById("configSaveStatusHint");
                        const detailRange = document.createRange();
                        detailRange.selectNodeContents(detailNode);
                        return {
                            state,
                            tabsTop: document.getElementById("configTabs").getBoundingClientRect().top,
                            detailHeight: detailNode.getBoundingClientRect().height,
                            detailWidth: detailNode.getBoundingClientRect().width,
                            detailScrollHeight: detailNode.scrollHeight,
                            detailTextHeight: detailRange.getBoundingClientRect().height,
                            detailLineHeight: Number.parseFloat(getComputedStyle(detailNode).lineHeight),
                            hintWidth: hintNode.getBoundingClientRect().width,
                            detailHidden: detailNode.hidden,
                        };
                    });
                })()`,
                returnByValue: true,
            });
            const metrics = layout.result.value;
            const tabsTop = metrics.map(({ tabsTop: value }) => value);
            assert.ok(
                Math.max(...tabsTop) - Math.min(...tabsTop) <= 0.5,
                `${viewport.name} Setup tabs must not move across save states: ${JSON.stringify(metrics)}`
            );
            metrics.forEach((metric) => {
                assert.equal(metric.detailHidden, false, `${viewport.name} detail slot must remain in layout`);
                assert.ok(
                    metric.detailHeight + 0.5 >= metric.detailScrollHeight,
                    `${viewport.name} detail slot must contain ${metric.state} feedback without growth: ${JSON.stringify(metric)}`
                );
                assert.ok(
                    Math.abs(metric.detailWidth - metric.hintWidth) <= 0.5,
                    `${viewport.name} detail and autosave hint must share a wrapping width: ${JSON.stringify(metric)}`
                );
            });
            if (viewport.name === "desktop") {
                const pausedMetric = metrics.find(({ state }) => state === "error");
                assert.ok(
                    pausedMetric.detailTextHeight <= pausedMetric.detailLineHeight + 0.5,
                    `desktop paused-save detail must use the available width: ${JSON.stringify(pausedMetric)}`
                );
            }
        }
        if (process.env.WSPRRYPI_UI_CAPTURE_DIR) {
            fs.mkdirSync(process.env.WSPRRYPI_UI_CAPTURE_DIR, { recursive: true });
            for (const viewport of [{name:"desktop",width:1280,height:900},{name:"mobile",width:375,height:812}]) {
                await client.send("Emulation.setDeviceMetricsOverride", {width:viewport.width,height:viewport.height,deviceScaleFactor:1,mobile:viewport.name==="mobile"});
                const prepared = await client.send("Runtime.evaluate", {expression:`(() => {
                    window.WtpUi = {...window.WtpUi,selected:()=>true,maximumJobDurationSeconds:3600,maximumJobEvents:512};
                    document.getElementById("qrss_mode").checked=true;
                    document.getElementById("mode_qrss").checked=true;
                    document.getElementById("mode_fskcw").checked=false;
                    document.getElementById("mode_dfcw").checked=false;
                    document.getElementById("dot_length").value="3";
                    document.getElementById("tx_repeat_every").value="60";
                    document.getElementById("qrss_message").value="?".repeat(32);
                    clickQRSSModeToggle();updateCwMessageLengthEstimate();validateCwMessage();
                    setConfigSaveStatus("", "", "");window.scrollTo(0,0);
                    return {overflow:document.documentElement.scrollWidth>innerWidth,valid:document.getElementById("qrss_message").checkValidity(),estimate:document.getElementById("cw_message_length_estimate").textContent};
                })()`,returnByValue:true});
                if (prepared.exceptionDetails) throw new Error(prepared.exceptionDetails.exception?.description || prepared.exceptionDetails.text);
                assert.equal(prepared.result.value.overflow,false);
                assert.equal(prepared.result.value.valid,true);
                assert.match(prepared.result.value.estimate,/32 \/ 32 characters/);
                const layout = await client.send("Page.getLayoutMetrics");
                const size = layout.cssContentSize || layout.contentSize;
                const full = await client.send("Page.captureScreenshot",{format:"png",captureBeyondViewport:true,clip:{x:0,y:0,width:size.width,height:size.height,scale:1}});
                fs.writeFileSync(path.join(process.env.WSPRRYPI_UI_CAPTURE_DIR,viewport.name+"-pi-message-32.png"),Buffer.from(full.data,"base64"));
                const bounds = await client.send("Runtime.evaluate",{expression:`(() => { const b=document.getElementById("qrss_message_set").getBoundingClientRect();return {x:Math.max(0,b.x),y:Math.max(0,b.y+scrollY),width:b.width,height:b.height,scale:1};})()`,returnByValue:true});
                const detail = await client.send("Page.captureScreenshot",{format:"png",captureBeyondViewport:true,clip:bounds.result.value});
                fs.writeFileSync(path.join(process.env.WSPRRYPI_UI_CAPTURE_DIR,viewport.name+"-pi-message-detail.png"),Buffer.from(detail.data,"base64"));
            }
        }
        console.log("cw_duration_latch_integration_test passed");
    } finally {
        if (client) client.close();
        if (chromium) chromium.kill("SIGTERM");
        php.kill("SIGTERM");
    }
}

main().catch((error) => {
    console.error(error.stack || error.message);
    process.exitCode = 1;
});
