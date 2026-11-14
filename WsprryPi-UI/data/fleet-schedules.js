// SPDX-License-Identifier: MIT
(function (root) {
    "use strict";
    async function readReply(response) {
        try {
            const value = await response.json();
            if (!value || typeof value !== "object" || Array.isArray(value)) throw new TypeError("Expected an object reply");
            return value;
        }
        catch (error) {
            if (["AbortError", "TimeoutError"].includes(error.name)) throw error;
            root.console?.warn("Controller reply could not be read", {status: response.status, error: error.name});
            throw new Error(response.ok
                ? "The controller returned an unreadable reply. Refresh and review its state before trying again."
                : `The controller is unavailable (HTTP ${response.status}). Check its connection before trying again.`);
        }
    }
    const byId = id => root.document.getElementById(id);
    const url = suffix => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/host/" + suffix;
    const number = name => Number(byId("fleet-schedule-" + name).value);
    const text = name => byId("fleet-schedule-" + name).value.trim();
    let data = null, revision = "", editorRevision = "", profiles = [], editId = "", busy = false, closed = false, visible = false, timer, observedAt = 0, listenerBusy = false;
    const rows = new Map();
    let pendingConfirmation = null, editorContext = null, draftTarget = null, draftDirty = false, editorAction = null, draftProfileBinding = null, draftProfileChanged = false;
    const draftKey = "wsprrypi.fleet.editor.draft.1";
    function profileBinding(profile) {
        if (!profile?.settings || typeof profile.settings !== "object") return null;
        return {id: profile.id, name: profile.name,
            settings: Object.fromEntries(Object.entries(profile.settings).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0))};
    }
    function preserveDraft() {
        if (byId("fleet-schedule-editor").hidden) return;
        const fields = {};
        byId("fleet-schedule-editor").querySelectorAll("input,select").forEach(node => {fields[node.id] = node.type === "checkbox" ? node.checked : node.value;});
        if (!editId && !draftProfileChanged) draftProfileBinding = profileBinding(profiles.find(p => p.id === fields["fleet-schedule-device"]));
        root.sessionStorage.setItem(draftKey, JSON.stringify({fields, editId, editorRevision, editorContext, draftTarget, editorAction, draftProfileBinding}));
        draftDirty = true;
    }
    async function restoreDraft() {
        if (closed || busy || !visible) return;
        let saved;try {saved = JSON.parse(root.sessionStorage.getItem(draftKey) || "null");} catch (_) {return;}
        if (!saved || typeof saved !== "object" || !saved.fields || typeof saved.editId !== "string") return;
        busy = true;setVisible(visible);message("Restoring unsaved entries…");
        try {
            const catalog = await fetchJson("devices");
            if (closed || !visible) return;
            profiles = catalog.value.profiles.filter(p => !p.legacy_unverified);
            const selectedId = saved.fields["fleet-schedule-device"];
            draftProfileBinding = saved.draftProfileBinding || null;
            draftProfileChanged = !saved.editId && !!selectedId && (!draftProfileBinding ||
                JSON.stringify(draftProfileBinding) !== JSON.stringify(profileBinding(profiles.find(p => p.id === selectedId))));
            const selector = byId("fleet-schedule-device");selector.replaceChildren();
            const placeholder = root.document.createElement("option");placeholder.value = "";placeholder.textContent = "Choose a saved output";selector.append(placeholder);
            for (const profile of profiles) {
                if (draftProfileChanged && profile.id === selectedId) continue;
                const option = root.document.createElement("option");option.value = profile.id;option.textContent = profile.name;selector.append(option);
            }
            if (draftProfileChanged) {
                const option = root.document.createElement("option");option.value = selectedId;
                option.textContent = draftProfileBinding ? `${draftProfileBinding.name} · ${draftProfileBinding.settings["Device ID"]} (changed)` : "Earlier output identity unavailable";
                selector.append(option);
            }
            if (saved.editId) {const option = root.document.createElement("option");option.value = "assigned";option.textContent = saved.draftTarget?.name || saved.editId;selector.append(option);}
            editorAction = saved.editorAction || null;
            editId = saved.editId;editorRevision = saved.editorRevision;editorContext = saved.editorContext;draftTarget = saved.draftTarget;
            for (const [id, value] of Object.entries(saved.fields)) {const node = byId(id);if (node && node.closest("#fleet-schedule-editor")) {if (node.type === "checkbox") node.checked = value === true;else if (typeof value === "string") node.value = value;}}
            byId("fleet-schedule-editor-heading").textContent = editId ? "Edit output schedule" : "Assign a schedule";
            byId("fleet-schedule-save").textContent = editId ? "Save schedule" : "Save assignment";
            byId("fleet-schedule-editor").hidden = false;draftDirty = true;modeFields();
            message(draftProfileChanged ? "Saved output changed or its original review is unavailable. Entries remain available for inspection. Discard this draft and reopen the intended output before saving." : "Unsaved entries restored with their original target and revision. Review current state before saving.", true);
        } catch (error) {message(`Draft retained; entries could not be restored: ${error.message}`, !byId("fleet-schedule-editor").hidden);}
        finally {
            busy = false;
            if (byId("fleet-schedule-feedback").textContent === "Restoring unsaved entries…") message("");
            if (!closed) {setVisible(visible);if (visible) load();}
        }
    }
    function confirmMutation(operation, assignment, trigger) {
        const panel = byId("fleet-action-confirmation");
        const label = operation === "remove" ? "Remove assignment" : "Pause and stop";
        byId("fleet-action-heading").textContent = label + "?";
        byId("fleet-action-target").textContent = assignment.name;
        byId("fleet-action-consequence").textContent = operation === "remove"
            ? "This removes its saved schedule. Any job owned by this controller will be stopped."
            : "This pauses its schedule. Any job owned by this controller will be stopped.";
        const confirm = byId("fleet-action-confirm");
        confirm.textContent = label;confirm.disabled = false;
        const modal = root.bootstrap.Modal.getOrCreateInstance(panel);
        return new Promise(resolve => {
            pendingConfirmation = {resolve, modal, trigger, accepted: false};
            modal.show();
        });
    }
    function cancelConfirmation() {
        if (!pendingConfirmation) return;
        pendingConfirmation.accepted = false;
        pendingConfirmation.modal.hide();
    }
    function message(value, editor = false) {const node=byId(editor ? "fleet-schedule-editor-feedback" : "fleet-schedule-feedback");if(node.textContent!==value)node.textContent=value;}
    async function fetchJson(suffix, method = "GET", body, etag) {
        const response = await root.fetch(url(suffix), {method, cache: "no-store", signal: AbortSignal.timeout(method === "GET" ? 7000 : 45000),
            ...(body ? {headers: {"Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": etag}, body: JSON.stringify(body)} : {})});
        const value = await readReply(response);
        if (!response.ok) throw Object.assign(new Error(response.status === 412 ? "Schedules changed. Refresh and review before trying again."
            : value.error?.message || value.error?.code || `Request failed (${response.status}).`), {status: response.status});
        return {value, etag: response.headers.get("ETag")};
    }
    function validCapacity(value) {
        return Number.isSafeInteger(value?.maximum_outputs) && value.maximum_outputs > 0;
    }
    function occupied(value) {
        return new Set([...value.assignments.map(row => row.device_id), ...Object.keys(value.outputs || {})]).size;
    }
    function validateFleet(value) {
        if (value.scope !== "wsprrypi-wtp-fleet/1" || !Array.isArray(value.assignments) ||
            !validCapacity(value) || value.assignments.length > value.maximum_outputs ||
            !value.outputs || typeof value.outputs !== "object" || Array.isArray(value.outputs)) {
            data = null;
            throw new Error("Invalid fleet status or capacity. Refresh schedules before assigning an output.");
        }
        return value;
    }
    function canAssign() {return data && validCapacity(data) && occupied(data) < data.maximum_outputs;}
    function createRow(id) {
        const row = root.document.createElement("section");
        row.className = "py-3 border-bottom";
        const name = root.document.createElement("h5");name.className = "h6 mb-1 text-break";
        const description = root.document.createElement("p");description.className = "mb-1";
        const status = root.document.createElement("p");status.className = "form-text text-break mb-2";
        const actions = root.document.createElement("div");actions.className = "d-flex flex-wrap gap-2";
        const buttons = {};
        for (const [action, label] of [["enable", "Resume"], ["pause", "Pause and stop"], ["recover", "Reconcile"], ["edit", "Edit schedule"], ["remove", "Remove assignment"], ["review", "Review current state"]]) {
            const button = root.document.createElement("button");button.type = "button";
            button.className = "btn btn-sm " + (action === "remove" ? "btn-outline-danger" : "btn-outline-secondary");
            button.textContent = label;
            button.addEventListener("click", () => action === "edit" ? openEditor(id) : action === "review" ? undefined : mutate(action, id, button));
            actions.append(button);buttons[action] = button;
            if (action === "review") button.addEventListener("click", () => {try {root.FleetClient.review(id);message("Current state reviewed; any unknown earlier result remains historical.");} catch (error) {message(error.message);}});
        }
        row.append(name, description, status, actions);
        return {row, name, description, status, buttons};
    }
    function render() {
        const list = byId("fleet-output-list");
        list.setAttribute("aria-busy", String(busy));
        byId("fleet-schedule-new").disabled = !visible || busy || !canAssign();
        byId("fleet-schedule-refresh").disabled = !visible || busy;
        byId("fleet-schedule-capacity").textContent = data && validCapacity(data)
            ? `${data.assignments.length} of ${data.maximum_outputs} remote outputs assigned. ${occupied(data) - data.assignments.length} unresolved contexts retained.${canAssign() ? "" : " Capacity reached; remove an assignment or reconcile unresolved outputs first."}`
            : "Capacity unavailable. Refresh schedules before assigning an output.";
        if (!data) {
            for (const elements of rows.values())
                for (const button of Object.values(elements.buttons)) button.disabled = true;
            return;
        }
        const retired = Object.entries(data.outputs || {}).filter(([id, output]) =>
            output.revoked && !data.assignments.some(row => row.device_id === id));
        const current = new Set([...data.assignments.map(row => row.device_id), ...retired.map(([id]) => id)]);
        for (const [id, elements] of rows) if (!current.has(id)) {elements.row.remove();rows.delete(id);}
        list.querySelector("[data-empty]")?.remove();
        for (const assignment of data.assignments) {
            const id = assignment.device_id;
            if (!rows.has(id)) {const elements = createRow(id);rows.set(id, elements);list.append(elements.row);}
            const elements = rows.get(id), schedule = assignment.schedule, output = data.outputs?.[id];
            const memberName=output?.usb?.hostname;
            elements.name.textContent = memberName?.saved || assignment.name;
            if (memberName?.saved) elements.name.textContent += output?.usb?.management?.connected && output?.usb?.name_confirmed ? "" : " (last confirmed)";
            elements.description.textContent = `${schedule.mode.toUpperCase()} · ${Number(schedule.frequency_hz).toLocaleString()} Hz · every ${schedule.period_seconds} s · phase ${schedule.phase_seconds} s UTC`;
            const observedAge = root.FleetClient.age(output);
            const age = observedAge === null ? "age unknown" : `${observedAge / 1000n} s ago`;
            const action = root.FleetClient.outcome(id, {controller_incarnation: data.controller_incarnation, assignment_generation: output?.assignment_generation}) || output?.actions?.at(-1);
            elements.status.textContent = `${assignment.enabled ? "Schedule enabled" : "Schedule paused"} · ${output?.state || "unavailable"}. ${output?.message || ""} WTP observation: ${age}.${action ? ` Action ${root.FleetClient.actionLabel(action)}: ${action.reason || ""}` : ""}`;
            for (const button of Object.values(elements.buttons)) {button.hidden = false;button.disabled = !visible || busy;}
            elements.buttons.enable.disabled ||= assignment.enabled || assignment.in_flight || !output?.can_resume || observedAge === null || observedAge >= 15000n || output?.actions?.some(a => a.work_pending);
            elements.buttons.pause.disabled ||= output?.can_pause === false || output?.actions?.some(a => a.operation === "pause" && a.work_pending);
            elements.buttons.recover.disabled ||= output?.can_reconcile === false;
            elements.buttons.edit.disabled ||= assignment.enabled || assignment.in_flight || output?.state === "recovery_required" || output?.actions?.some(a => a.work_pending);
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
        if (closed || busy || !visible) return;
        try {await root.FleetClient.refresh();}
        catch (error) {message(`Fleet status unavailable: ${error.message}`);}
    }
    async function mutate(operation, id, trigger) {
        if (closed || busy || !visible) return;
        const assignment = data?.assignments.find(row => row.device_id === id);
        if (!assignment && !(operation === "recover" && data?.outputs?.[id]?.revoked)) return;
        // The confirmation refers to this revision, even if another operator
        // changes the assignment while the dialog is open.
        const reviewedContext = root.FleetClient.capture(id);
        busy = true;clearTimeout(timer);setVisible(visible);
        try {
            if (["pause", "remove"].includes(operation) &&
                !await confirmMutation(operation, assignment, trigger)) return;
            if (closed || !visible) return;
            message(`${operation === "recover" ? "Requesting reconciliation" : "Updating assignment"}…`);
            const action = await root.FleetClient.mutate({operation, device_id: id}, reviewedContext);
            message(action.state === "confirmed" ? "Requested postconditions confirmed." : "Action accepted. Review its correlated result before resuming.");
        } catch (error) {message(`${error.message} Read status before repeating an unconfirmed action.`);}
        finally {
            busy = false;setVisible(visible);
            if (!closed && visible) {
                if (trigger?.isConnected && !trigger.disabled) trigger.focus();
                else byId("fleet-schedule-refresh").focus();
                load();
            }
        }
    }
    function modeFields() {
        const mode = text("mode");
        const frequencyText = {
            wspr: ["WSPR RF center (Hz)", "Absolute RF center, not USB dial or tone 0. For 20m use 14097100 Hz; no dial conversion is applied."],
            tone: ["Carrier frequency (Hz)", "One steady RF carrier. The target must support this frequency."],
            qrss: ["QRSS carrier (Hz)", "RF carrier frequency. No WSPR placement is applied."],
            fskcw: ["FSKCW space (Hz)", "Space frequency; mark = space + positive shift."],
            dfcw: ["DFCW dot (Hz)", "Dot frequency; dash = dot + positive shift."]
        }[mode];
        byId("fleet-schedule-frequency-label").textContent = frequencyText[0];
        byId("fleet-schedule-frequency-help").textContent = frequencyText[1];
        byId("fleet-schedule-editor").querySelectorAll("[data-schedule-modes]").forEach(group => {
            group.hidden = !group.dataset.scheduleModes.split(" ").includes(mode);
            group.querySelectorAll("input").forEach(field => {field.disabled = !visible || busy || group.hidden;});
        });
        const profile = profiles.find(p => p.id === text("device"));
        byId("fleet-schedule-plain-field").hidden = !!editId || profile?.settings.Transport !== "network_plain";
        byId("fleet-schedule-management-field").hidden = !!editId || profile?.settings.Transport === "usb";
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
        if (closed || busy || !visible || !data) return;
        if (!id && !canAssign()) return;
        if (draftDirty || root.sessionStorage.getItem(draftKey)) {
            const captured = id ? root.FleetClient.capture(id) : null;
            byId("fleet-draft-choice").hidden = false;
            byId("fleet-draft-discard").onclick = () => {
                if (closed || busy || !visible) return;
                try {
                    if (captured) {const current = root.FleetClient.capture(id);
                        if (current.controller_incarnation !== captured.controller_incarnation || current.assignment_generation !== captured.assignment_generation || current.reviewed_control_revision !== captured.reviewed_control_revision)
                            throw new Error("Selected output changed. Read and review before discarding entries for another schedule.");}
                    root.sessionStorage.removeItem(draftKey);draftDirty = false;byId("fleet-draft-choice").hidden = true;openEditor(id);
                } catch (error) {message(error.message, true);}
            };
            byId("fleet-draft-continue").onclick = () => {if (closed || busy || !visible) return;byId("fleet-draft-choice").hidden = true;restoreDraft();};
            byId("fleet-draft-cancel").onclick = () => {if (!closed && !busy && visible) byId("fleet-draft-choice").hidden = true;};
            byId("fleet-draft-continue").focus();return;
        }
        const assignment = id ? data.assignments.find(row => row.device_id === id) : null;
        if (id && (!assignment || assignment.enabled || assignment.in_flight)) return;
        // Bind the draft to the schedule displayed when it was opened. Status
        // polling must not silently authorize overwriting another operator.
        const reviewedRevision = revision;
        const reviewedContext = id ? root.FleetClient.capture(id) : null;
        const catalogChoice = byId("fleet-device")?.value || "";
        busy = true;clearTimeout(timer);setVisible(visible);
        try {
            const saved = await fetchJson("devices");
            if (closed || !visible) return;
            const nextProfiles = saved.value.profiles.filter(p => !p.legacy_unverified);
            if (!id && !nextProfiles.length) {message("Add and identify a device in Known and nearby devices first.");return;}
            // A failed catalog refresh must leave any existing draft bound to
            // its original target and revision.
            profiles = nextProfiles;editId = id;editorRevision = reviewedRevision;draftProfileBinding = null;draftProfileChanged = false;
            editorContext = reviewedContext;draftTarget = assignment;editorAction = null;
            const selector = byId("fleet-schedule-device");selector.replaceChildren();
            const placeholder = root.document.createElement("option");
            placeholder.value = "";placeholder.textContent = "Choose a saved output";selector.append(placeholder);
            for (const profile of profiles) {const option = root.document.createElement("option");option.value = profile.id;option.textContent = profile.name;selector.append(option);}
            selector.disabled = !!id;
            if (!id && catalogChoice.startsWith("known:") && profiles.some(p => p.id === catalogChoice.slice(6)))
                selector.value = catalogChoice.slice(6);
            if (id) {
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
        } catch (error) {message(error.message);}
        finally {
            busy = false;setVisible(visible);
            if (!closed && visible) {
                if (!byId("fleet-schedule-editor").hidden) byId(editId ? "fleet-schedule-mode" : "fleet-schedule-device").focus();
                load();
            }
        }
    }
    async function save() {
        if (busy || !visible) return;
        if (draftProfileChanged) {message("Saved output changed. Discard this draft and reopen the intended output before saving.", true);return;}
        if (!editId && !canAssign()) {message("Capacity unavailable or reached. Refresh schedules and review before saving. Your draft is preserved.", true);return;}
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
            const reviewed = editorContext || {device_id: profile.settings["Device ID"], controller_incarnation: data.controller_incarnation,
                assignment_generation: null, reviewed_control_revision: null, reviewed_boot_id: null, etag: editorRevision};
            const action = await root.FleetClient.mutate(command, reviewed);
            editorAction = {action_id: action.action_id, device_id: action.device_id};
            preserveDraft();
            if (action.state === "confirmed") {root.sessionStorage.removeItem(draftKey);draftDirty = false;byId("fleet-schedule-editor").hidden = true;}
            message("Save accepted. Entries remain available until its correlated result confirms persistence.", true);
        } catch (error) {
            const detail = error.status === 412 ? "Schedules changed. Cancel this draft, reopen the editor, and review the current settings." : error.message;
            message(`${detail} Your draft is preserved.`, true);
        }
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
        root.FleetClient.subscribe(state => {
            if (closed) return;
            if (state.valid && state.data) {data = validateFleet(state.data);revision = state.etag;observedAt = Date.now();
                if (byId("fleet-schedule-feedback").textContent.startsWith("Fleet status unavailable:")) message("");
            }
            else {data = null;if (state.error) message(`Fleet status unavailable: ${state.error}`);}
            if (visible) render();
            if (draftDirty && editorAction) {
                const result = root.FleetClient.outcome(editorAction.device_id);
                if (result?.action_id === editorAction.action_id && result.state === "confirmed") {
                    root.sessionStorage.removeItem(draftKey);draftDirty = false;byId("fleet-schedule-editor").hidden = true;message("Output schedule saved.");
                } else if (result && ["refused", "superseded", "outcome_unconfirmed"].includes(result.state)) {
                    message(`${result.state.replaceAll("_", " ")}: ${result.reason || "Review the current output state."} Your draft is preserved. Read and review before another save.`, true);
                }
            }
        });
        byId("fleet-schedule-editor").addEventListener("input", preserveDraft);
        byId("fleet-schedule-editor").addEventListener("change", preserveDraft);
        byId("fleet-tab").addEventListener("shown.bs.tab", load);
        byId("fleet-schedule-refresh").addEventListener("click", load);
        byId("fleet-schedule-new").addEventListener("click", () => openEditor());
        byId("fleet-schedule-mode").addEventListener("change", modeFields);
        byId("fleet-schedule-device").addEventListener("change", modeFields);
        byId("fleet-schedule-save").addEventListener("click", save);
        byId("fleet-schedule-editor-review").addEventListener("click", () => {
            const id = editId || profiles.find(p => p.id === text("device"))?.settings["Device ID"];
            try {root.FleetClient.review(id);message("Current state reviewed. Entries retain their original revision; reopen them if the assignment changed.", true);} catch (error) {message(error.message, true);}
        });
        byId("fleet-schedule-cancel").addEventListener("click", () => {preserveDraft();byId("fleet-schedule-editor").hidden = true;byId("fleet-schedule-new").focus();});
        byId("fleet-listener").addEventListener("toggle", () => {if (byId("fleet-listener").open) loadListener();});
        byId("fleet-listener-save").addEventListener("click", saveListener);
        const confirmation = byId("fleet-action-confirmation");
        confirmation.addEventListener("shown.bs.modal", () => {
            if (closed || !visible) cancelConfirmation();
            else byId("fleet-action-cancel").focus();
        });
        confirmation.addEventListener("hidden.bs.modal", () => {
            const pending = pendingConfirmation;
            pendingConfirmation = null;
            pending?.resolve(pending.accepted);
        });
        byId("fleet-action-confirm").addEventListener("click", () => {
            if (!pendingConfirmation || closed || !visible) return;
            pendingConfirmation.accepted = true;
            byId("fleet-action-confirm").disabled = true;
            pendingConfirmation.modal.hide();
        });
        root.document.querySelectorAll("[data-independent-wtp]").forEach(panel => {
            for (const event of ["input", "change"]) panel.addEventListener(event, e => e.stopPropagation());
        });
        root.addEventListener("pagehide", () => {closed = true;clearTimeout(timer);cancelConfirmation();});
        root.addEventListener("pageshow", event => {
            if (!event.persisted) return;
            closed = false;data = null;setVisible(visible);load();
        });
        setVisible(true);
        let handoff;try {handoff = JSON.parse(root.sessionStorage.getItem("wsprrypi.fleet.editor.handoff") || "null");root.sessionStorage.removeItem("wsprrypi.fleet.editor.handoff");} catch (_) {}
        if (handoff) openAssignedEditor(handoff).catch(error => message(error.message));
        else if (root.sessionStorage.getItem(draftKey)) restoreDraft();
    }
    function setVisible(value) {
        visible = value === true;
        for (const id of ["fleet-schedules", "fleet-listener"]) {
            const panel = byId(id);
            if (!panel) continue;
            panel.inert = !visible;
            panel.querySelectorAll("input,select,button").forEach(control => {control.disabled = !visible || busy || (id === "fleet-listener" && listenerBusy);});
        }
        if (visible) {modeFields();byId("fleet-schedule-device").disabled = busy || !!editId || draftProfileChanged;byId("fleet-schedule-save").disabled ||= draftProfileChanged;render();}
        else {clearTimeout(timer);cancelConfirmation();}
    }
    async function openAssignedEditor(target) {
        await root.FleetClient.refresh();
        const current = root.FleetClient.capture(target.device_id);
        if (current.controller_incarnation !== target.controller_incarnation || current.assignment_generation !== target.assignment_generation || current.etag !== target.etag)
            throw new Error("Selected schedule changed during navigation. Read and review before editing.");
        root.sessionStorage.setItem("wsprrypi.fleet.return_focus", "operationDevice");
        await openEditor(target.device_id);
    }
    root.FleetSchedules = {setVisible, openAssignedEditor};
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(window);
