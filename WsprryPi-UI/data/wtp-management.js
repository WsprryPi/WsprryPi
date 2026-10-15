// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    function schedules(text) {
        if (!text.trim()) return [];
        const rows = text.trim().split(/\n/).map(line => {
            const values = line.trim().split(/\s+/);
            if (values.length !== 2 || values.some(v => !/^\d+$/.test(v))) throw new Error("Enter a period and phase on each line.");
            const [period_s, phase_s] = values.map(Number);
            if (period_s < 120 || period_s > 86400 || 86400 % period_s || period_s % 120 || phase_s >= period_s || phase_s % 120)
                throw new Error("Periods must divide one day; period and phase must be multiples of 120 seconds.");
            return { period_s, phase_s };
        });
        if (rows.length > 8) throw new Error("Use at most eight schedules.");
        return rows;
    }
    function changes(saved, draft) {
        if (!saved) return draft;
        const patch = {};
        for (const [key, value] of Object.entries(draft)) {
            if (key === "station" || key === "wifi") {
                const leaves = {};
                for (const [leaf, next] of Object.entries(value)) {
                    const previous = saved[key]?.[leaf] ?? (leaf === "password" ? null : "");
                    if (JSON.stringify(previous) !== JSON.stringify(next)) leaves[leaf] = next;
                }
                if (Object.keys(leaves).length) patch[key] = leaves;
            } else if (JSON.stringify(saved[key] ?? (key === "expires_utc_s" ? 0 : null)) !== JSON.stringify(value)) patch[key] = value;
        }
        return patch;
    }
    if (typeof module !== "undefined" && module.exports) module.exports = { schedules, changes };
    if (!root.document) return;
    const byId = id => root.document.getElementById(id);
    let available = false, busy = false, loaded = false, reconcile = false;
    let canSelect = false, consumerConnected = false, explicitDisconnected = false, selectionObserved = false;
    let configRevision = "", networkRevision = "", saved = null, target = null;
    const field = name => byId(`wtp-remote-${name}`);
    const originalHints = new Map();
    function failure(message) { const error = new Error(message); error.operatorMessage = true; return error; }
    function render() {
        if (!byId("wtp-management")) return;
        byId("wtp-management").hidden = !root.WtpUi?.developmentControlsVisible;
        resourceRender();
        const carrier = byId("wtp-management-carrier");
        const consumer = carrier && carrier.value !== "engineering_mtls";
        for (const id of ["wtp-management-host-field", "wtp-management-password-field"]) if (byId(id)) byId(id).hidden = !consumer;
        for (const id of ["wtp-management-carrier", "wtp-management-host", "wtp-management-password", "wtp-management-connect", "wtp-management-disconnect", "wtp-management-state"])
            if (byId(id)) byId(id).disabled = busy || !canSelect || (id === "wtp-management-connect" && consumerConnected);
        byId("wtp-management").setAttribute("aria-busy", String(busy));
        root.document.querySelectorAll("[data-wtp-remote]").forEach(input => { input.disabled = busy || !available || !loaded; });
        for (const id of ["wtp-remote-load", "wtp-network-load"]) byId(id).disabled = busy || !available;
        for (const id of ["wtp-remote-save", "wtp-schedules-save"]) byId(id).disabled = busy || !available || !configRevision;
        byId("wtp-network-disable").disabled = busy || !available || !networkRevision;
        const availability = byId("wtp-management-availability");
        if (availability) {
            availability.hidden = available;
            const message = "Management is unavailable until the host is idle and Pico state is confirmed.";
            if (!available && availability.textContent !== message) availability.textContent = message;
        }
    }
    function values(c) {
        c = c || { enabled: false, station: {}, wifi: {}, schedules: [{ period_s: 120, phase_s: 0 }] };
        return { callsign: c.station.callsign || "", locator: c.station.locator || "", power: c.station.power_dbm ?? 20,
            ssid: c.wifi.ssid || "", ntp: c.wifi.ntp_ipv4 || "", expiry: c.expires_utc_s || 0,
            schedules: c.schedules.map(s => `${s.period_s} ${s.phase_s}`).join("\n"), enabled: c.enabled === true };
    }
    function populate(c, keepDraft) {
        for (const [id, value] of Object.entries(values(c))) {
            const hint = byId(`wtp-remote-${id}-hint`);
            if (keepDraft) {
                if (hint) hint.textContent = `Saved: ${value === "" ? "empty" : value}. Your draft is preserved.`;
            } else {
                if (id === "enabled") field(id).checked = value;
                else field(id).value = value;
                if (hint) hint.textContent = originalHints.get(id) || "";
            }
        }
        if (!keepDraft) field("password").value = "";
    }
    function restartFeedback(value) {
        return value === true ? " Some saved network or pin settings require a Pico restart; no restart was requested."
            : value === false ? " Saved changes apply to later admissions without a restart."
            : " Restart requirements are unconfirmed; read Pico status before operating.";
    }
    let resources = null, resourceRevision = "", resourceContext = "", resourceLoaded = false, resourceReconcile = false, validatedPlan = "";
    let resourceObserved = "";
    const member = name => byId(`wtp-member-${name}`);
    const resourceIdentity = data => `${data.target.device_id} / ${data.target.boot_id} / ${{engineering_mtls:"Engineering HTTPS",consumer_station:"Consumer Wi-Fi",consumer_ap:"Consumer setup Wi-Fi"}[data.carrier]}${data.profile_generation === null ? "" : ` / profile ${data.profile_generation}`}`;
    function frequencyText(value) {
        const nhz = BigInt(value), fraction = String(nhz % 1000000000n).padStart(9,"0").replace(/0+$/,"");
        return `${nhz / 1000000000n}${fraction ? "." + fraction : ""} Hz`;
    }
    function invalidateResources() {
        resourceRevision = resourceContext = validatedPlan = "";
        resourceReconcile = resourceLoaded;
        if (resourceLoaded) {
            member("feedback").textContent = "These observations are from the previous load. Your drafts are preserved; load member resources before another save.";
            member("identity").textContent = `Readback required. Previous observation ${resourceObserved}: ${resourceIdentity(resources)}`;
        }
    }
    function invalidateEditors() {
        configRevision = networkRevision = ""; target = null; reconcile = loaded;
        invalidateResources();
    }
    function hardwareCandidate() {
        const gp = name => member(name).value === "" ? null : Number(member(name).value);
        return { engine: "direct", rf_gp: gp("rf"), i2c_pair: null, button_gp: gp("button"),
            amplifier_gp: null, lpf_gps: [], indicator: member("indicator").value,
            indicator_gp: member("indicator").value === "external" ? gp("indicator-gp") : null,
            indicator_active_high: member("polarity").value === "true" };
    }
    function planText(p) {
        if (p === null) return "Unknown / unavailable";
        const pin = n => n === null ? "disabled" : `GP${n}`;
        return `${p.engine === "direct" ? `Direct PIO RF ${pin(p.rf_gp)}` : `I2C pair ${p.i2c_pair}`}; button ${pin(p.button_gp)}; indicator ${{onboard_led:"onboard LED",external:"external",disabled:"disabled"}[p.indicator]}${p.indicator === "external" ? ` ${pin(p.indicator_gp)}, active ${p.indicator_active_high === false ? "low" : "high"}` : ""}`;
    }
    function resourceRender() {
        if (!member("load")) return;
        member("load").disabled = busy || !available;
        const h = resources?.application.hardware;
        const previewReady = available && resourceRevision && resources;
        const writable = available && resourceRevision && h?.saved && h.storage_healthy && resources.application.station.storage_healthy && !h.application_error;
        root.document.querySelectorAll("[data-wtp-resource]").forEach(input => {
            input.disabled = busy || (input.dataset.wtpResource === "station" ? !writable || resources.profile_owned_station : !previewReady);
        });
        member("station-save").disabled = busy || !writable || resources?.profile_owned_station;
        member("validate").disabled = busy || !previewReady;
        member("hardware-save").disabled = busy || !writable || !validatedPlan || validatedPlan !== JSON.stringify(hardwareCandidate());
        for (const name of ["indicator-gp", "polarity"]) member(name).disabled = busy || !previewReady || member("indicator").value !== "external";
        const roles = {rf:"RF", button:"button", "indicator-gp":"indicator"};
        for (const [name, role] of Object.entries(roles)) for (const option of member(name).options || []) {
            const owner = Object.keys(roles).find(other => other !== name && option.value !== "" && member(other).value === option.value &&
                (other !== "indicator-gp" || member("indicator").value === "external"));
            option.disabled = Boolean(owner);
            option.textContent = option.value === "" ? (name === "button" ? "Disabled" : "Select GP") : `GP${option.value}${owner ? ` (${roles[owner]})` : ""}`;
        }
    }
    function resourcePopulate(data) {
        const a = data.application, h = a.hardware, station = a.station.station;
        const keep = resourceLoaded && (resourceReconcile || !a.station.storage_healthy || !h.storage_healthy || h.application_error !== null);
        resourceObserved = new Date().toLocaleTimeString();
        member("identity").textContent = `Observed ${resourceObserved}: ${resourceIdentity(data)}`;
        member("recurrence").textContent = `Member-owned; ${a.recurrence.enabled ? "enabled" : "disabled"}; ${a.recurrence.suspended ? "STOP suspended" : "not suspended"}; expiry ${a.recurrence.expires_utc_s}; base ${frequencyText(a.recurrence.schedule_base_frequency_nhz)}; schedule periods/phases: ${a.recurrence.schedules.map(s => `${s.period_s}/${s.phase_s} s`).join(", ") || "none"}. Live observations may change independently.`;
        member("health").textContent = `Station storage ${a.station.storage_healthy ? "healthy" : "faulted"}; hardware storage ${h.storage_healthy ? "healthy" : "faulted"}; ${h.application_error || "no hardware application fault reported"}; execution engine ${h.execution_engine}.`;
        member("saved").textContent = `${planText(h.saved)}; saved generation ${h.saved_generation ?? "unavailable"}.`;
        member("active").textContent = `${planText(h.active)}; observation ${h.active_revision ?? "unavailable"}.`;
        member("restart").textContent = `${h.pending_restart ? "Pins pending restart; output inhibition remains latched for this boot." : "No pin restart latch reported."} ${a.reboot_required ? "Application restart required." : "No application restart reported."} No restart requested.`;
        member("station-feedback").textContent = !a.station.storage_healthy
            ? "Station storage is faulted; saved values are unavailable. Any retained fields are previous drafts. Resolve the storage fault before saving."
            : data.profile_owned_station
            ? `Read-only: Pico transactional setup owns the effective station. ${station ? `${station.callsign}, ${station.locator}, ${station.power_dbm} dBm.` : "Effective station unavailable."}`
            : station ? `Saved: ${station.callsign}, ${station.locator}, ${station.power_dbm} dBm.${keep ? " Your draft is preserved; review before saving." : " Stored extended callsigns can be valid configuration while unsupported by the Type 1 station-write path."}`
                : "No saved station. Use the existing setup workflow first.";
        member("pairs").textContent = data.pin_choices.i2c_pairs.map(p => `Pair ${p.id}: I2C${p.controller} GP${p.sda_gp}/GP${p.scl_gp}`).join("; ");
        if (!keep) {
            if (station) for (const [name,value] of Object.entries({callsign:station.callsign,locator:station.locator,power:station.power_dbm})) member(name).value = String(value);
            const options = (name, optional) => {
                member(name).innerHTML = `<option value="">${optional ? "Disabled" : "Select GP"}</option>` + data.pin_choices.eligible_gps.map(gp => `<option value="${gp}">GP${gp}</option>`).join("");
            };
            options("rf", false); options("button", true); options("indicator-gp", false);
            if (h.saved) for (const [name,value] of Object.entries({rf:h.saved.rf_gp,button:h.saved.button_gp,indicator:h.saved.indicator,"indicator-gp":h.saved.indicator_gp,polarity:h.saved.indicator_active_high !== false}))
                member(name).value = value === null ? "" : String(value);
            else for (const name of ["rf","button","indicator","indicator-gp","polarity"]) member(name).value = "";
        }
        resources = data; resourceLoaded = true; resourceReconcile = false; validatedPlan = "";
        member("pin-feedback").textContent = "Candidate has not been validated. Validation never saves or activates pins.";
        member("hardware-feedback").textContent = keep ? "Your hardware draft is preserved beside fresh saved and active observations. Validate it before saving." : "";
    }
    async function resourceSave(kind) {
        if (!resourceRevision || !resourceContext || !resources) return;
        const station = kind === "station";
        if (station && resources.profile_owned_station) return;
        if (!station && (!validatedPlan || validatedPlan !== JSON.stringify(hardwareCandidate()))) return;
        const payload = station ? { callsign:member("callsign").value, locator:member("locator").value, power_dbm:Number(member("power").value) } : hardwareCandidate();
        return request(`member/${kind}`, "PUT", {schema:`transmitter-${kind}/1`, target:resources.target, [station ? "station" : "pins"]:payload}, resourceRevision);
    }
    async function request(resource, method = "GET", body = null, revision = "") {
        if (busy || (resource === "member/management" ? !canSelect : !available)) return;
        busy = true; render();
        const shared = ["member/application", "member/station", "member/hardware", "member/pins/validate"].includes(resource);
        const feedback = shared ? member(resource.endsWith("validate") ? "pin-feedback" : method === "PUT" ? `${resource.split("/")[1]}-feedback` : "feedback")
            : byId(resource === "member/management" ? "wtp-management-connection-feedback" : "wtp-management-feedback");
        const context = resourceContext;
        if (method !== "GET" && !resource.endsWith("validate")) invalidateEditors();
        feedback.textContent = "Contacting the idle Pico…";
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 180000);
        try {
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/" + resource, {
                method, cache: "no-store", signal: controller.signal,
                ...(body ? { headers: { "Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": revision, ...(shared ? { "X-WsprryPi-Member-Context": context } : {}) }, body: JSON.stringify(body) } : {})
            });
            const data = await response.json();
            if (!response.ok) {
                const code = data.error?.code;
                if (resource === "member/management") {
                    if (code === "management_session_already_selected") {
                        selectionObserved = false;
                        throw failure("A management session is already active. Read its connection state or disconnect it before selecting another connection.");
                    }
                    consumerConnected = false; explicitDisconnected = true; available = false;
                    throw failure(code === "radio_session_busy"
                        ? "Pico already has a browser or client session. Log out there or wait for it to expire; this client did not close it."
                        : code === "host_busy_or_output_unresolved"
                            ? "Host work or unresolved output blocks management. Reconcile that work before connecting."
                            : code === "unsupported_management_contract"
                                ? "This Pico does not advertise the supported management contract. No fallback was attempted."
                                : "Consumer login or connection was not confirmed. Check the exact Pico ID, address and saved Wi-Fi password. No fallback was attempted.");
                }
                if (["consumer_login_required", "consumer_session_unconfirmed", "management_carrier_required"].includes(code)) {
                    consumerConnected = false; explicitDisconnected = true; available = false;
                    throw failure("Consumer session expired or was not confirmed. Your draft is preserved. Log in again, then load saved Pico settings before saving.");
                }
                if (shared && code === "shared_resources_unavailable") throw failure("This firmware does not advertise shared resources. Use the supported Pico settings form below; no replacement write was attempted.");
                if (shared && code === "unsupported_station_encoding") throw failure("The complete station cannot be encoded by the current Type 1 write path. Your callsign and locator draft is preserved without truncation. Load resources before another save.");
                if (shared && code === "output_disable_failed") throw failure("The new settings may be saved, but output shutdown was not confirmed. Load resources to inspect saved and active plans. Restart/output inhibition remains latched; no restart was requested.");
                if (shared && code === "storage_fault") throw failure("Pico storage reported a fault. The save result is unconfirmed; load member resources to inspect trusted saved state. Your draft is preserved.");
                if (shared && method === "PUT" && data.mutation_attempted === true && response.status >= 500) throw failure("The save result is unconfirmed; new settings may already be stored. Your draft is preserved. Load member resources before another explicit save. No automatic replay occurred.");
                if (shared && ["unsupported_resource_capabilities","unsupported_pin_capabilities","invalid_remote_resource","invalid_remote_pins","invalid_pin_validation","unsupported_management_contract"].includes(code)) throw failure("This Pico's resources could not be validated safely. Your draft is preserved; load resources before saving. No automatic fallback was attempted.");
                throw failure(response.status === 412
                    ? "Pico settings changed elsewhere. Your draft is preserved. Load saved Pico settings before saving again."
                    : code === "unsupported_configuration_updates" || code === "unsupported_legacy_field" || code === "invalid_remote_configuration"
                        ? "This Pico's settings cannot be preserved safely. No automatic fallback was attempted. Your draft is preserved."
                        : ["host_busy_or_output_unresolved", "remote_busy_or_output_unresolved", "busy"].includes(code)
                            ? "Pico management is busy or output is unresolved. Finish or reconcile that work, then load saved settings. Your draft is preserved."
                        : ["target_mismatch", "configuration_context_changed", "member_identity_unavailable", "member_read_required"].includes(code)
                            ? "The selected Pico or its boot context changed. Load saved settings before saving. Your draft is preserved."
                        : code === "consumer_setup_identity_required"
                            ? "Pico setup owns these station or Wi-Fi values. Use Pico setup to change them. Your draft is preserved."
                            : "The Pico request was not confirmed. Your draft is preserved. Load saved Pico settings and review them before saving again.");
            }
            const etag = response.headers.get("ETag") || "";
            if (shared) {
                if (method === "GET") {
                    if (data.schema !== "wsprrypi-member-resources/1" || !data.application || !data.target || typeof data.member_context !== "string" || !etag)
                        throw failure("Member resources are incomplete. Your draft is preserved; load resources again.");
                    resourcePopulate(data); resourceRevision = etag; resourceContext = data.member_context;
                    feedback.textContent = "Member resources loaded. Saved configuration was reconciled under one revision. Station and hardware are separate explicit saves.";
                } else if (resource.endsWith("validate")) {
                    if (typeof data.valid !== "boolean" || typeof data.operational_supported !== "boolean" || data.applied !== false) throw failure("Pin validation was not confirmed.");
                    validatedPlan = data.valid && data.operational_supported ? JSON.stringify(body) : "";
                    feedback.textContent = data.valid ? data.operational_supported ? "Candidate is conflict-free and supported by the software adapter. Preview only; no settings or pins changed. Save member hardware to persist it."
                        : "Candidate is structurally eligible, but its adapter is unavailable. Nothing was saved or applied."
                        : `Candidate conflict: ${data.error}; role ${data.role}${data.gp === null ? "" : `, GP${data.gp}`}${data.owner ? ` owned by ${data.owner}` : ""}. Nothing was saved or applied.`;
                } else {
                    feedback.textContent = `Member ${resource.split("/")[1]} saved.${data.pending_restart === true ? " Pins require restart; active routing is unchanged and output inhibition remains latched." : ""} Load member resources before another save. Other drafts are preserved. No restart or transmission requested.`;
                }
            } else if (resource === "member/management") {
                consumerConnected = data.connected === true;
                if (method === "GET") {
                    if (!["engineering_mtls", "consumer_station", "consumer_ap", "unsupported", "disconnected"].includes(data.carrier) || typeof data.connected !== "boolean" || typeof data.host !== "string")
                        throw failure("Management connection state could not be read. Select a connection explicitly.");
                    explicitDisconnected = ["unsupported", "disconnected"].includes(data.carrier) || (data.carrier.startsWith("consumer_") && !data.connected);
                    if (consumerConnected && byId("wtp-management-carrier")) {
                        byId("wtp-management-carrier").value = data.carrier;
                        byId("wtp-management-host").value = data.host;
                    }
                } else explicitDisconnected = body.action === "disconnect";
                available = canSelect && (consumerConnected || (!explicitDisconnected && root.WtpUi?.read().Transport === "network"));
                invalidateEditors();
                feedback.textContent = consumerConnected
                    ? "Consumer management authenticated for this Pico and boot. Load saved Pico settings before editing. The login password has been cleared."
                    : explicitDisconnected ? "Management disconnected. Only this client's consumer session was closed. Your settings draft is preserved."
                        : "Engineering HTTPS selected. Load saved Pico settings before editing.";
            } else if (resource === "network") {
                networkRevision = etag;
                const discovery = data.mdns_state ? ` Discovery: ${data.mdns_state}. Configured hostname: ${data.configured_hostname || "none"}. Advertised hostname: ${data.advertised_hostname || "none"}. Current address: ${data.ipv4 || "unavailable"}.` : "";
                const problem = data.mdns_state === "conflict" ? " Resolve the name conflict, then retry the same certified name with an idle disable/re-enable or restart. No automatic rename occurred."
                    : data.mdns_state === "failed" ? ` Discovery failed (${data.mdns_reason || "unconfirmed reason"}); inspect the Pico deployment before retrying.` : "";
                feedback.textContent = `Pico Wi-Fi: ${data.enabled ? "enabled" : "disabled"}.${discovery}${problem} ${method === "PUT" ? "Network control may now disconnect; use USB Console or restart to reconnect." : "This is a remote observation."}`;
                if (method === "PUT") { networkRevision = ""; configRevision = ""; reconcile = true; }
            } else {
                if (data.schema !== "wsprrypi-pico-settings/1" || !data.target || !Object.hasOwn(data, "config") || !etag)
                    throw failure("Pico settings context is incomplete. Your draft is preserved; load saved settings again.");
                configRevision = etag; target = data.target;
                if (method === "GET") {
                    const keepDraft = reconcile && loaded;
                    saved = data.config;
                    populate(saved, keepDraft);
                    loaded = true; reconcile = false;
                    feedback.textContent = keepDraft
                        ? "Saved Pico settings read. Your draft and password entry are preserved. Review the saved values before saving again."
                        : saved ? `Saved Pico settings loaded. ${data.update_strategy === "merge" ? "Pico supports settings-preserving edits." : "Compatible legacy saves preserve understood settings."}`
                            : "Pico has no saved standalone settings. Complete the fields before saving.";
                    feedback.textContent += restartFeedback(data.reboot_required);
                } else {
                    if (body.operation === "save") field("password").value = "";
                    saved = data.config; networkRevision = "";
                    feedback.textContent = (body.operation === "schedules" ? "Pico schedules saved. Other drafts are preserved." : "Pico settings saved.") + restartFeedback(data.reboot_required);
                }
            }
        } catch (error) {
            // No retry or strategy switch: even a failed delivery can follow a commit.
            if (shared) invalidateResources();
            if (resource !== "network" || method !== "GET") { configRevision = ""; target = null; reconcile = loaded; }
            if (resource === "network") networkRevision = "";
            feedback.textContent = error.name === "AbortError"
                ? "Pico request timed out. Its result is unconfirmed. Your draft is preserved; read saved settings before retrying a change."
                : error.operatorMessage ? error.message
                    : "Pico request or response could not be confirmed. Your draft is preserved; load saved settings before saving again.";
        } finally {
            if (resource === "member/management" && method === "POST" && byId("wtp-management-password")) byId("wtp-management-password").value = "";
            clearTimeout(timeout); busy = false; render();
        }
    }
    function save(onlySchedules) {
        if (!configRevision || !target) return;
        try {
            const entries = schedules(field("schedules").value);
            let config = { schedules: entries };
            if (!onlySchedules) {
                const expiry = Number(field("expiry").value), power = Number(field("power").value);
                if (!Number.isSafeInteger(expiry) || expiry < 0 || expiry > 2147483647 || !Number.isInteger(power)) throw new Error("Enter whole-number power and expiry values; expiry must be at most 2147483647.");
                config = changes(saved, { version: 1, enabled: field("enabled").checked,
                    station: { callsign: field("callsign").value, locator: field("locator").value, power_dbm: power },
                    wifi: { ssid: field("ssid").value, ntp_ipv4: field("ntp").value,
                        password: field("password").value || (saved ? null : "") },
                    schedules: entries, expires_utc_s: expiry });
                if (!Object.keys(config).length) { byId("wtp-management-feedback").textContent = "No Pico settings changed."; return; }
            }
            return request("member/configuration", "POST", { schema: "wsprrypi-pico-settings/1", target,
                operation: onlySchedules ? "schedules" : "save", config }, configRevision);
        } catch (error) { byId("wtp-management-feedback").textContent = error.message; }
    }
    root.WtpManagement = { setAvailability(value, selectable = value) {
        canSelect = selectable;
        available = !explicitDisconnected && (consumerConnected ? selectable : value);
        render();
        if (selectable && !selectionObserved && !busy) {
            selectionObserved = true;
            void request("member/management");
        }
    } };
    function init() {
        if (!byId("wtp-management")) return;
        for (const id of Object.keys(values(null))) originalHints.set(id, byId(`wtp-remote-${id}-hint`)?.textContent || "");
        byId("wtp-management-carrier")?.addEventListener("change", () => {
            if (byId("wtp-management-password")) byId("wtp-management-password").value = "";
            render();
        });
        byId("wtp-management-connect")?.addEventListener("click", () => {
            const carrier = byId("wtp-management-carrier").value;
            return request("member/management", "POST", carrier === "engineering_mtls"
                ? { action: "engineering_mtls" }
                : { action: "connect", carrier, host: byId("wtp-management-host").value,
                    password: byId("wtp-management-password").value });
        });
        byId("wtp-management-state")?.addEventListener("click", () => request("member/management"));
        byId("wtp-management-disconnect")?.addEventListener("click", () => request("member/management", "POST", { action: "disconnect" }));
        member("load")?.addEventListener("click", () => request("member/application"));
        member("station-save")?.addEventListener("click", () => resourceSave("station"));
        member("hardware-save")?.addEventListener("click", () => resourceSave("hardware"));
        member("validate")?.addEventListener("click", () => request("member/pins/validate", "POST", hardwareCandidate(), resourceRevision));
        for (const name of ["rf", "button", "indicator", "indicator-gp", "polarity"]) member(name)?.addEventListener("change", () => {
            validatedPlan = ""; member("pin-feedback").textContent = "Candidate changed. Validate the complete plan before saving."; render();
        });
        byId("wtp-remote-load").addEventListener("click", () => request("member/configuration"));
        byId("wtp-network-load").addEventListener("click", () => request("network"));
        byId("wtp-remote-save").addEventListener("click", () => save(false));
        byId("wtp-schedules-save").addEventListener("click", () => save(true));
        byId("wtp-network-disable").addEventListener("click", () => request("network", "PUT", { enabled: false }, networkRevision));
        render();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(typeof window === "undefined" ? globalThis : window);
