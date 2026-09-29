// SPDX-License-Identifier: MIT
(function (root) {
    "use strict";
    const byId = id => root.document.getElementById(id);
    const url = suffix => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/host/" + suffix;
    const number = name => Number(byId("fleet-schedule-" + name).value);
    const text = name => byId("fleet-schedule-" + name).value.trim();
    let data = null, revision = "", profiles = [], editId = "", busy = false, closed = false, visible = false, timer, observedAt = 0, listenerBusy = false;
    const rows = new Map();
    function message(value, editor = false) {byId(editor ? "fleet-schedule-editor-feedback" : "fleet-schedule-feedback").textContent = value;}
    async function fetchJson(suffix, method = "GET", body, etag) {
        const response = await root.fetch(url(suffix), {method, cache: "no-store", signal: AbortSignal.timeout(method === "GET" ? 7000 : 45000),
            ...(body ? {headers: {"Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": etag}, body: JSON.stringify(body)} : {})});
        const value = await response.json();
        if (!response.ok) throw new Error(response.status === 412 ? "Schedules changed. Refresh and review before saving again. Your draft is preserved."
            : value.error?.message || value.error?.code || `Request failed (${response.status}).`);
        return {value, etag: response.headers.get("ETag")};
    }
    function createRow(id) {
        const row = root.document.createElement("section");
        row.className = "py-3 border-bottom";
        const name = root.document.createElement("h5");name.className = "h6 mb-1";
        const description = root.document.createElement("p");description.className = "mb-1";
        const status = root.document.createElement("p");status.className = "form-text text-break mb-2";
        const actions = root.document.createElement("div");actions.className = "d-flex flex-wrap gap-2";
        const buttons = {};
        for (const [action, label] of [["enable", "Resume"], ["pause", "Pause and stop"], ["recover", "Reconcile"], ["edit", "Edit schedule"], ["remove", "Remove assignment"]]) {
            const button = root.document.createElement("button");button.type = "button";
            button.className = "btn btn-sm " + (action === "remove" ? "btn-outline-danger" : "btn-outline-secondary");
            button.textContent = label;
            button.addEventListener("click", () => action === "edit" ? openEditor(id) : mutate(action, id));
            actions.append(button);buttons[action] = button;
        }
        row.append(name, description, status, actions);
        return {row, name, description, status, buttons};
    }
    function render() {
        const list = byId("fleet-output-list");
        list.setAttribute("aria-busy", String(busy));
        byId("fleet-schedule-new").disabled = !visible || busy || !data || data.assignments.length >= 8;
        byId("fleet-schedule-refresh").disabled = !visible || busy;
        if (!data) return;
        const retired = Object.entries(data.outputs || {}).filter(([id, output]) =>
            output.revoked && !data.assignments.some(row => row.device_id === id));
        const current = new Set([...data.assignments.map(row => row.device_id), ...retired.map(([id]) => id)]);
        for (const [id, elements] of rows) if (!current.has(id)) {elements.row.remove();rows.delete(id);}
        list.querySelector("[data-empty]")?.remove();
        for (const assignment of data.assignments) {
            const id = assignment.device_id;
            if (!rows.has(id)) {const elements = createRow(id);rows.set(id, elements);list.append(elements.row);}
            const elements = rows.get(id), schedule = assignment.schedule, output = data.outputs?.[id];
            elements.name.textContent = assignment.name;
            elements.description.textContent = `${schedule.mode.toUpperCase()} · ${Number(schedule.frequency_hz).toLocaleString()} Hz · every ${schedule.period_seconds} s · phase ${schedule.phase_seconds} s UTC`;
            const observed = output?.wtp?.status_observed_ms;
            const age = observed && data.now_ms ? `${Math.max(0, Math.round((Number(data.now_ms) - Number(observed) + Date.now() - observedAt) / 1000))} s ago` : "age unknown";
            elements.status.textContent = `${assignment.enabled ? "Schedule enabled" : "Schedule paused"} · ${output?.state || "unavailable"}. ${output?.message || ""} WTP observation: ${age}.`;
            for (const button of Object.values(elements.buttons)) button.disabled = !visible || busy;
            elements.buttons.enable.disabled ||= assignment.enabled || assignment.in_flight;
            elements.buttons.pause.disabled ||= !assignment.enabled && !assignment.in_flight;
            elements.buttons.edit.disabled ||= assignment.enabled || assignment.in_flight;
        }
        for (const [id, output] of retired) {
            if (!rows.has(id)) {const elements = createRow(id);rows.set(id, elements);list.append(elements.row);}
            const elements = rows.get(id);
            elements.name.textContent = "Removed output " + id.slice(0, 8);
            elements.description.textContent = "The saved assignment was removed after local takeover. Controller cleanup is still unresolved.";
            elements.status.textContent = output.message || "Reconcile this context before assigning the output again.";
            for (const [action, button] of Object.entries(elements.buttons)) {
                button.hidden = action !== "recover";
                button.disabled = !visible || busy;
            }
        }
        if (!current.size) {
            list.replaceChildren();
            const empty = root.document.createElement("p");empty.className = "form-text";empty.dataset.empty = "";
            empty.textContent = "No remote schedules assigned. This Pi's local schedule remains available.";list.append(empty);
        }
        const removals = data.removals || [];
        if (removals.length && !busy) message(`${removals.length} assignment${removals.length === 1 ? " was" : "s were"} removed after local takeover. Reassign only when the target is available again.`);
    }
    async function load() {
        if (closed || busy || !root.WtpUi?.developmentControlsVisible) return;
        clearTimeout(timer);
        try {
            const result = await fetchJson("fleet");
            if (result.value.scope !== "wsprrypi-wtp-fleet/1" || !Array.isArray(result.value.assignments)) throw new Error("Invalid fleet status response.");
            data = result.value;revision = result.etag;observedAt = Date.now();
            if (!rows.size) byId("fleet-output-list").replaceChildren();
            render();
        } catch (error) {message(`Fleet status unavailable: ${error.message}`);}
        finally {if (!closed) timer = setTimeout(load, 3000);}
    }
    async function mutate(operation, id) {
        if (busy || !visible) return;
        const assignment = data?.assignments.find(row => row.device_id === id);
        if (!assignment && !(operation === "recover" && data?.outputs?.[id]?.revoked)) return;
        if (["pause", "remove"].includes(operation) && !root.confirm(`${operation === "remove" ? "Remove the assignment for" : "Pause and stop"} ${assignment.name}? Any job owned by this controller will be stopped.`)) return;
        busy = true;render();message(`${operation === "recover" ? "Requesting reconciliation" : "Updating assignment"}…`);
        try {
            const result = await fetchJson("fleet", "POST", {operation, device_id: id}, revision);
            data = result.value;revision = result.etag;observedAt = Date.now();
            message(operation === "recover" ? "Reconciliation requested. Review the output status before resuming." : "Assignment updated.");
        } catch (error) {message(`${error.message} Read status before repeating an unconfirmed action.`);}
        finally {busy = false;render();load();}
    }
    function modeFields() {
        const mode = text("mode");
        byId("fleet-schedule-editor").querySelectorAll("[data-schedule-modes]").forEach(group => {
            group.hidden = !group.dataset.scheduleModes.split(" ").includes(mode);
            group.querySelectorAll("input").forEach(field => {field.disabled = !visible || busy || group.hidden;});
        });
        const profile = profiles.find(p => p.id === text("device"));
        byId("fleet-schedule-plain-field").hidden = !!editId || profile?.settings.Transport !== "network_plain";
        byId("fleet-schedule-management-field").hidden = !!editId;
        byId("fleet-schedule-enable-field").hidden = !!editId;
    }
    function schedule() {
        const result = {mode: text("mode"), frequency_hz: number("frequency"), period_seconds: number("period"), phase_seconds: number("phase")};
        if (result.mode === "tone") result.duration_ms = Math.round(number("duration") * 1000);
        else if (result.mode === "wspr") Object.assign(result, {callsign: text("callsign"), locator: text("locator"), power_dbm: number("power")});
        else {
            Object.assign(result, {message: text("message"), dot_ms: Math.round(number("dot") * 1000)});
            if (result.mode !== "qrss") result.shift_hz = number("shift");
        }
        return result;
    }
    async function openEditor(id = "") {
        if (busy || !visible) return;
        editId = id;
        try {
            const saved = await fetchJson("devices");profiles = saved.value.profiles.filter(p => !p.legacy_unverified);
            const selector = byId("fleet-schedule-device");selector.replaceChildren();
            for (const profile of profiles) {const option = root.document.createElement("option");option.value = profile.id;option.textContent = profile.name;selector.append(option);}
            if (!id && !profiles.length) {message("Add and identify a device in Known and nearby devices first.");return;}
            selector.disabled = !!id;
            if (id) {
                const assignment = data.assignments.find(row => row.device_id === id);
                const option = root.document.createElement("option");option.value = "assigned";option.textContent = assignment.name;selector.append(option);selector.value = "assigned";
                const s = assignment.schedule;
                for (const [field, key] of [["mode", "mode"], ["frequency", "frequency_hz"], ["period", "period_seconds"], ["phase", "phase_seconds"], ["callsign", "callsign"], ["locator", "locator"], ["power", "power_dbm"], ["message", "message"], ["shift", "shift_hz"]])
                    if (s[key] !== undefined) byId("fleet-schedule-" + field).value = s[key];
                if (s.duration_ms) byId("fleet-schedule-duration").value = s.duration_ms / 1000;
                if (s.dot_ms) byId("fleet-schedule-dot").value = s.dot_ms / 1000;
            }
            byId("fleet-schedule-editor-heading").textContent = id ? "Edit output schedule" : "Assign a schedule";
            byId("fleet-schedule-save").textContent = id ? "Save schedule" : "Save assignment";
            byId("fleet-schedule-enabled").checked = false;byId("fleet-schedule-plain").checked = false;
            byId("fleet-schedule-editor").hidden = false;message("", true);modeFields();
            byId(id ? "fleet-schedule-mode" : "fleet-schedule-device").focus();
        } catch (error) {message(error.message);}
    }
    async function save() {
        if (busy || !visible) return;
        const fields = [...byId("fleet-schedule-editor").querySelectorAll("input,select")].filter(field => !field.disabled && !field.closest("[hidden]"));
        for (const field of fields) if (!field.reportValidity()) return;
        const profile = profiles.find(p => p.id === text("device"));
        if (!editId && !profile) {message("Choose a saved output.", true);return;}
        if (!editId && profile.settings.Transport === "network_plain" && !byId("fleet-schedule-plain").checked) {message("Confirm Plain LAN for this assignment.", true);return;}
        const command = editId ? {operation: "schedule", device_id: editId, schedule: schedule()} : {
            operation: "assign", name: profile.name, settings: profile.settings, schedule: schedule(),
            enabled: byId("fleet-schedule-enabled").checked, consent_plain: byId("fleet-schedule-plain").checked,
            management_port: number("management")};
        busy = true;setVisible(visible);message("Checking the output and saving…", true);
        try {
            const result = await fetchJson("fleet", "POST", command, revision);
            data = result.value;revision = result.etag;observedAt = Date.now();byId("fleet-schedule-editor").hidden = true;
            message("Output schedule saved.");byId("fleet-schedule-new").focus();
        } catch (error) {message(`${error.message} Your draft is preserved.`, true);}
        finally {busy = false;setVisible(visible);load();}
    }
    let listenerRevision = "";
    async function loadListener() {
        try {
            const result = await fetchJson("config");listenerRevision = result.etag;
            const settings = result.value.config["WTP Server"];
            byId("fleet-listener-enabled").checked = settings.Enabled;
            byId("fleet-listener-port").value = settings.Port;
            byId("fleet-listener-interface").value = settings.Interface;
            const state = (await fetchJson("wtp-endpoint")).value;
            byId("fleet-listener-feedback").textContent = state.listener_running
                ? `Listening on ${state.address}:${state.port}. DNS-SD ${state.dns_sd_published ? "published" : "unavailable; direct connections remain usable"}.`
                : state.listener_error || state.error || "Listener is disabled.";
        } catch (error) {byId("fleet-listener-feedback").textContent = error.message;}
    }
    async function saveListener() {
        if (!listenerRevision || !visible || listenerBusy) return;
        listenerBusy = true;
        const button = byId("fleet-listener-save");button.disabled = true;
        try {
            if (!byId("fleet-listener-port").reportValidity()) return;
            const result = await fetchJson("config", "PUT", {"WTP Server": {Enabled: byId("fleet-listener-enabled").checked,
                Port: Number(byId("fleet-listener-port").value), Interface: byId("fleet-listener-interface").value.trim()}}, listenerRevision);
            listenerRevision = result.etag;root.WtpUi?.setHostRevision(result.etag);
            byId("fleet-listener-feedback").textContent = "Listener settings saved. An existing remote owner may defer applying the new route; refresh this section to read status.";
        } catch (error) {byId("fleet-listener-feedback").textContent = `${error.message} Your draft is preserved.`;}
        finally {listenerBusy = false;button.disabled = !visible;}
    }
    function init() {
        if (!byId("fleet-schedules")) return;
        byId("fleet-tab").addEventListener("shown.bs.tab", load);
        byId("fleet-schedule-refresh").addEventListener("click", load);
        byId("fleet-schedule-new").addEventListener("click", () => openEditor());
        byId("fleet-schedule-mode").addEventListener("change", modeFields);
        byId("fleet-schedule-device").addEventListener("change", modeFields);
        byId("fleet-schedule-save").addEventListener("click", save);
        byId("fleet-schedule-cancel").addEventListener("click", () => {byId("fleet-schedule-editor").hidden = true;byId("fleet-schedule-new").focus();});
        byId("fleet-listener").addEventListener("toggle", () => {if (byId("fleet-listener").open) loadListener();});
        byId("fleet-listener-save").addEventListener("click", saveListener);
        root.document.querySelectorAll("[data-independent-wtp]").forEach(panel => {
            for (const event of ["input", "change"]) panel.addEventListener(event, e => e.stopPropagation());
        });
        root.addEventListener("pagehide", () => {closed = true;clearTimeout(timer);});
        setVisible(root.WtpUi?.developmentControlsVisible === true);
    }
    function setVisible(value) {
        visible = value === true;
        for (const id of ["fleet-schedules", "fleet-listener"]) {
            const panel = byId(id);
            if (!panel) continue;
            panel.inert = !visible;
            panel.querySelectorAll("input,select,button").forEach(control => {control.disabled = !visible || busy || (id === "fleet-listener" && listenerBusy);});
        }
        if (visible) {modeFields();byId("fleet-schedule-device").disabled = busy || !!editId;render();}
        else clearTimeout(timer);
    }
    root.FleetSchedules = {setVisible};
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(window);
