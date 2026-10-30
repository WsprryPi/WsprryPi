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
    function stationHint() {
        return "Save the complete station together. The Pico automatically selects WSPR Type 1, 2 or 3 from the callsign and locator. Reported power is message metadata; it does not set measured RF output.";
    }
    if (typeof module !== "undefined" && module.exports) module.exports = { schedules, changes, stationHint };
    if (!root.document) return;
    const byId = id => root.document.getElementById(id);
    let available = false, busy = false, loaded = false, reconcile = false;
    let canSelect = false, consumerConnected = false, explicitDisconnected = false, selectionObserved = false;
    let configRevision = "", networkRevision = "", saved = null, target = null;
    const field = name => byId(`wtp-remote-${name}`);
    let retirement = null, selectionId = "", nativeSession = null, managementCarrier = "";
    let configReview = false, resourceReview = false, lastAttempt = null;
    const originalHints = new Map();
    function failure(message) { const error = new Error(message); error.operatorMessage = true; return error; }
    function render() {
        if (!byId("wtp-management")) return;
        byId("wtp-management").hidden = false;
        resourceRender();
        const carrier = byId("wtp-management-carrier");
        const consumer = carrier && carrier.value.startsWith("consumer_");
        for (const id of ["wtp-management-host-field", "wtp-management-password-field"]) if (byId(id)) byId(id).hidden = !consumer;
        for (const id of ["wtp-management-carrier", "wtp-management-host", "wtp-management-password", "wtp-management-connect", "wtp-management-disconnect", "wtp-management-state"])
            if (byId(id)) byId(id).disabled = busy || !canSelect || (id === "wtp-management-connect" && consumerConnected);
        if (byId("wtp-management-recovery")) byId("wtp-management-recovery").hidden = !consumer;
        if (byId("wtp-management-inspect")) byId("wtp-management-inspect").disabled = busy || !canSelect;
        if (byId("wtp-management-retire")) byId("wtp-management-retire").disabled = busy || !canSelect || !retirement || !byId("wtp-management-target").value;
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
            ssid: c.wifi.ssid || "", ntp: c.wifi.ntp_ipv4 || "pool.ntp.org", expiry: c.expires_utc_s || 0,
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
    let progress = null, progressRevision = "", progressEpoch = 0, progressLoaded = false, progressReview = false, stationDirty = false;
    const member = name => byId(`wtp-member-${name}`);
    const resourceIdentity = data => `${data.target.device_id} / ${data.target.boot_id} / ${{engineering_mtls:"Engineering HTTPS",consumer_station:"Consumer Wi-Fi",consumer_ap:"Consumer setup Wi-Fi",usb_physical:"USB"}[data.carrier]}${data.profile_generation === null ? "" : ` / profile ${data.profile_generation}`}`;
    function frequencyText(value) {
        const nhz = BigInt(value), fraction = String(nhz % 1000000000n).padStart(9,"0").replace(/0+$/,"");
        return `${nhz / 1000000000n}${fraction ? "." + fraction : ""} Hz`;
    }
    let progressDraftTarget = "", progressTargetMismatch = false;
    const progressControl = name => byId(`wtp-usb-progress-${name}`);
    const stationEdits = {callsign:0,locator:0,power:0};
    const stationNames = {callsign:"callsign",locator:"locator",power:"power_dbm"};
    function validProgress(value) {
        const keys = "contract,schema,target,draft_revision,config_revision,profile_revision,fields,last_receipt,last_outcomes,applied_station,station,station_complete,complete,apply_domains,wspr_plan,schedule_enabled,schedule_suspended,matches_applied".split(",");
        const sequence = v => typeof v === "string" && /^(0|[1-9][0-9]{0,19})$/.test(v) && BigInt(v) <= 18446744073709551615n;
        return value && Object.keys(value).every(k => keys.includes(k) || ["outcomes","request_id"].includes(k)) && keys.every(k => Object.hasOwn(value,k)) &&
            value.contract === "Wsprry-Control/1" && value.schema === "progress" && value.target?.scope === "member" &&
            /^[0-9a-f]{32}$/.test(value.target.device_id) && /^[0-9a-f]{32}$/.test(value.target.boot_id) &&
            ["draft_revision","config_revision","profile_revision"].every(k => sequence(value[k])) &&
            value.station && Object.keys(value.station).sort().join(",") === "callsign,locator,power_dbm" &&
            ["callsign","locator"].every(k => value.station[k] === null || typeof value.station[k] === "string") &&
            (value.station.power_dbm === null || [0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60].includes(value.station.power_dbm)) &&
            ["station_complete","complete","matches_applied","schedule_enabled","schedule_suspended"].every(k => typeof value[k] === "boolean") &&
            value.fields && typeof value.fields === "object" && !Array.isArray(value.fields) &&
            value.apply_domains && Object.keys(value.apply_domains).sort().join(",") === "all,drive,network,radio,station" &&
            Object.values(value.apply_domains).every(v => typeof v === "boolean");
    }
    function invalidateProgress() {
        ++progressEpoch; progressRevision = "";
        if (progressLoaded) progressReview = true;
    }
    function progressRender() {
        if (!progressControl("load")) return;
        progressControl("load").disabled = busy || !available;
        const ready = available && progressRevision && progress;
        progressControl("save").disabled = busy || !ready || progressTargetMismatch;
        const profileOwned = managementCarrier.startsWith("consumer_");
        progressControl("apply").disabled = busy || !ready || progressTargetMismatch || profileOwned || progress.draft_revision === "0" || progress.apply_domains.station !== true;
        if (progressControl("discard")) progressControl("discard").disabled = busy || !ready;
        if (progressLoaded) member("station-hint").textContent = "Save valid station fields independently; incomplete fields remain drafts. Apply saved station progress separately. Complete station save remains available. Reported WSPR power is message metadata, separate from drive and measured RF output.";
    }
    function progressSummary(value) {
        const text = station => station ? `${station.callsign ?? "unset"}, ${station.locator ?? "unset"}, ${station.power_dbm ?? "unset"} dBm` : "none";
        return `Saved progress: ${text(value.station)}. Applied station: ${text(value.applied_station)}. Draft ${value.draft_revision}; station ${value.station_complete ? "complete" : "incomplete"}; full configuration ${value.complete ? "complete" : "incomplete"}. STOP ${value.schedule_suspended ? "suspended" : "not suspended"}. Saving progress does not establish transmit readiness. Consumer profile station changes require Pico setup.`;
    }
    async function stationProgress(action) {
        if (busy || !available || (action !== "load" && !progressRevision)) return;
        if (action === "discard") {
            if (!root.confirm("Replace these station drafts with this Pico's saved progress?")) return;
            for (const [name,key] of Object.entries(stationNames)) {
                member(name).value = progress.station[key] ?? "";
                member(`${name}-progress`).textContent = "Device-saved progress loaded into this draft.";
            }
            progressDraftTarget = JSON.stringify(progress.target); progressTargetMismatch = stationDirty = false;
            render(); return;
        }
        if (action !== "load" && (progressTargetMismatch || progressReview && !root.confirm("Review saved progress beside your preserved draft. Send one new explicit change using this fresh read?"))) return;
        if (action === "apply" && progressControl("apply").disabled) return;
        const values = Object.fromEntries(Object.keys(stationNames).map(name => [name,member(name).value]));
        const stamps = {...stationEdits};
        const prior = progress, revision = progressRevision;
        let payload;
        if (action === "save") {
            const power = values.power;
            payload = {contract:'Wsprry-Control/1',schema:"progress",target:prior.target,
                request_id:Array.from(root.crypto.getRandomValues(new Uint8Array(16)),b=>b.toString(16).padStart(2,"0")).join(""),
                fields:{callsign:values.callsign || null,locator:values.locator || null,power_dbm:power === "" ? null : Number.isSafeInteger(Number(power)) ? Number(power) : power}};
        } else if (action === "apply") payload = {contract:'Wsprry-Control/1',schema:"progress",target:prior.target,
            application:{draft_revision:prior.draft_revision,domain:"station"}};
        const needsReview = progressReview;
        if (action !== "load") invalidateEditors(); else progressRevision = "";
        const ticket = ++progressEpoch;
        busy = true; render(); progressControl("state").textContent = action === "load" ? "Reading saved station progress…" : action === "save" ? "Saving valid station fields…" : "Applying device-saved station progress…";
        try {
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/member/progress" + (action === "load" ? "" : `/${action === "save" ? "update" : "apply"}`), {
                method:action === "load" ? "GET" : "POST",cache:"no-store",signal:AbortSignal.timeout(120000),
                ...(payload ? {headers:{"Content-Type":"application/json","X-WsprryPico-Request":"1","If-Match":revision},body:JSON.stringify(payload)} : {})
            });
            const result = await response.json();
            if (ticket !== progressEpoch) return;
            if (!response.ok) {
                const code = result.error?.code;
                if (["saved_progress_unavailable","unsupported_saved_progress"].includes(code)) progressLoaded = false;
                throw new Error(code === "saved_progress_unavailable" ? "This firmware has no saved-progress API. Use the existing complete station/settings save."
                    : code === "unsupported_saved_progress" ? "This firmware's saved-progress contract is unsupported. No change was sent. Use compatible firmware or the existing complete settings workflow."
                    : code === "consumer_setup_identity_required" ? "Pico setup owns the applied station. Your saved progress is retained; use Pico transactional setup to apply it."
                    : code === "storage_fault" ? "Pico storage is faulted. Resolve the fault, then read saved progress. Your draft is preserved."
                    : action === "load" ? "Saved station progress could not be read. Check the selected firmware and management connection, then read again. Your draft is preserved; this read did not save settings."
                    : "Station progress is rejected or unconfirmed. It may already be saved. Read saved progress and review before another change; your draft is preserved.");
            }
            const etag = response.headers.get("ETag") || "";
            if (action === "apply") {
                if (!etag || !result.config || Object.keys(prior.station).some(key => result.config.station?.[key] !== prior.station[key])) throw new Error("Station application was not confirmed. Read saved progress and member resources before another change.");
                progressReview = true;
                for (const name of Object.keys(stationNames)) member(`${name}-progress`).textContent = "Device-saved station applied. Your visible draft was not sent.";
                progressControl("state").textContent = "Device-saved station progress applied. Read saved progress and member resources to inspect current applied settings and restart requirements. No restart, STOP resume or transmission requested.";
                return;
            }
            if (!validProgress(result) || !etag || (payload && JSON.stringify(result.target) !== JSON.stringify(payload.target))) throw new Error("Station progress reply is incomplete or belongs to another Pico. Read saved progress before another change.");
            if (action === "save" && (result.request_id !== payload.request_id || !result.outcomes ||
                Object.keys(result.outcomes).sort().join(",") !== "callsign,locator,power_dbm" ||
                Object.values(result.outcomes).some(v => !["saved","cleared","invalid"].includes(v)))) throw new Error("Station save outcomes were not confirmed. Read saved progress before another change.");
            progress = result; progressLoaded = true;
            if (action === "load") {
                progressReview = needsReview;
                progressTargetMismatch = stationDirty && progressDraftTarget !== "" && progressDraftTarget !== "null" && progressDraftTarget !== JSON.stringify(result.target);
                if (!stationDirty) {
                    for (const [name,key] of Object.entries(stationNames)) if (stationEdits[name] === stamps[name]) member(name).value = result.station[key] ?? "";
                    progressDraftTarget = JSON.stringify(result.target);
                }
                for (const [name,key] of Object.entries(stationNames)) member(`${name}-progress`).textContent = `Device-saved progress: ${result.station[key] ?? "unset"}. ${stationDirty ? "Your draft is preserved." : ""}`;
                progressRevision = etag;
            } else {
                progressReview = true;
                for (const [name,key] of Object.entries(stationNames)) member(`${name}-progress`).textContent = action === "save"
                    ? `${{saved:"Saved as progress",cleared:"Saved as unset",invalid:"Invalid; previous progress retained"}[result.outcomes[key]]}.${stationEdits[name] !== stamps[name] ? " Your newer edit has not been saved." : ""}`
                    : "Device-saved station applied. Your visible draft was not sent.";
            }
            progressControl("state").textContent = progressSummary(result) + (progressTargetMismatch
                ? " These station drafts belong to a different Pico or boot. Discard and load this Pico's saved progress before saving."
                : action === "load" ? " Review device-saved values beside your draft before changing them."
                    : " Read saved progress and member resources before another change. No restart or transmission requested.");
        } catch (error) {
            if (ticket !== progressEpoch) return;
            invalidateProgress(); progressReview = true;
            progressControl("state").textContent = error.message || "Station progress unconfirmed. Read back before another change; your draft is preserved.";
        } finally { busy = false; render(); }
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
        driveEditor?.invalidate(); timingEditor?.invalidate(); filterEditor?.invalidate();
        root.WtpUsbSettings?.invalidate();
        root.WsprryMemberUdp?.invalidate();
        invalidateProgress();
        configRevision = networkRevision = ""; target = null; reconcile = loaded;
        invalidateResources();
    }
    function hardwareCandidate() {
        const gp = name => member(name).value === "" ? null : Number(member(name).value);
        return { engine: "direct", rf_gp: gp("rf"), i2c_pair: null, button_gp: gp("button"),
            amplifier_gp: gp("amplifier"), amplifier_active_high: member("amplifier-polarity").value === "true", lpf_gps: [], indicator: member("indicator").value,
            indicator_gp: member("indicator").value === "external" ? gp("indicator-gp") : null,
            indicator_active_high: member("polarity").value === "true" };
    }
    function planText(p) {
        if (p === null) return "Unknown / unavailable";
        const pin = n => n === null ? "disabled" : `GP${n}`;
        return `${p.engine === "direct" ? `Direct PIO RF ${pin(p.rf_gp)}` : `I2C pair ${p.i2c_pair}`}; amplifier ${pin(p.amplifier_gp)}, active ${p.amplifier_active_high === false ? "low" : "high"}; button ${pin(p.button_gp)}; indicator ${{onboard_led:"onboard LED",external:"external",disabled:"disabled"}[p.indicator]}${p.indicator === "external" ? ` ${pin(p.indicator_gp)}, active ${p.indicator_active_high === false ? "low" : "high"}` : ""}`;
    }
    function resourceRender() {
        if (!member("load")) return;
        member("load").disabled = busy || !available;
        member("station-hint").textContent = stationHint(resourceRevision ? resources?.application.schema : null);
        const h = resources?.application.hardware;
        const previewReady = available && resourceRevision && resources;
        const progressReady = available && progressRevision && progress;
        const writable = available && resourceRevision && h?.saved && h.storage_healthy && resources.application.station.storage_healthy && !h.application_error;
        root.document.querySelectorAll("[data-wtp-resource]").forEach(input => {
            input.disabled = busy || (input.dataset.wtpResource === "station" ? (!progressReady && (!(writable || (previewReady && resources?.carrier === "usb_physical" && resources.application.station.storage_healthy)) || resources.profile_owned_station)) : !previewReady);
        });
        member("amplifier-polarity").disabled = busy || !previewReady || member("amplifier").value === "";
        member("station-save").disabled = busy || !writable || resources?.profile_owned_station;
        progressRender();
        member("validate").disabled = busy || !previewReady;
        member("hardware-save").disabled = busy || !writable || !validatedPlan || validatedPlan !== JSON.stringify(hardwareCandidate());
        for (const name of ["indicator-gp", "polarity"]) member(name).disabled = busy || !previewReady || member("indicator").value !== "external";
        const roles = {rf:"RF", button:"button", amplifier:"amplifier", "indicator-gp":"indicator"};
        for (const [name, role] of Object.entries(roles)) for (const option of member(name).options || []) {
            const owner = Object.keys(roles).find(other => other !== name && option.value !== "" && member(other).value === option.value &&
                (other !== "indicator-gp" || member("indicator").value === "external"));
            option.disabled = Boolean(owner);
            option.textContent = option.value === "" ? (name === "button" ? "Disabled" : "Select GP") : `GP${option.value}${owner ? ` (${roles[owner]})` : ""}`;
        }
    }
    function resourcePopulate(data) {
        driveEditor?.setPolicy(data.control_capabilities?.gpio_drive);
        timingEditor?.setPolicy(data.control_capabilities?.amplifier_timing);
        const a = data.application, h = a.hardware, station = a.station.station;
        const keep = resourceLoaded && (resourceReconcile || !a.station.storage_healthy || !h.storage_healthy || h.application_error !== null);
        resourceObserved = new Date().toLocaleTimeString();
        member("identity").textContent = `Observed ${resourceObserved}: ${resourceIdentity(data)}`;
        member("recurrence").textContent = `Member-owned; ${a.recurrence.enabled ? "enabled" : "disabled"}; ${a.recurrence.suspended ? "STOP suspended" : "not suspended"}; expiry ${a.recurrence.expires_utc_s}; WSPR dial ${a.recurrence.schedule_dial_frequency_nhz === "0" ? "not configured" : frequencyText(a.recurrence.schedule_dial_frequency_nhz)}; schedule periods/phases: ${a.recurrence.schedules.map(s => `${s.period_s}/${s.phase_s} s`).join(", ") || "none"}. Live observations may change independently.`;
        member("health").textContent = `Station storage ${a.station.storage_healthy ? "healthy" : "faulted"}; hardware storage ${h.storage_healthy ? "healthy" : "faulted"}; ${h.application_error || "no hardware application fault reported"}; execution engine ${h.execution_engine}.`;
        member("saved").textContent = `${planText(h.saved)}; saved generation ${h.saved_generation ?? "unavailable"}.`;
        member("active").textContent = `${planText(h.active)}; observation ${h.active_revision ?? "unavailable"}.`;
        member("restart").textContent = `${h.pending_restart ? "Pins pending restart; output inhibition remains latched for this boot." : "No pin restart latch reported."} ${a.reboot_required ? "Application restart required." : "No application restart reported."} No restart requested.`;
        member("station-feedback").textContent = !a.station.storage_healthy
            ? "Station storage is faulted; saved values are unavailable. Any retained fields are previous drafts. Resolve the storage fault before saving."
            : data.profile_owned_station
            ? `Read-only: Pico transactional setup owns the effective station. ${station ? `${station.callsign}, ${station.locator}, ${station.power_dbm} dBm.` : "Effective station unavailable."}`
            : station ? `Saved: ${station.callsign}, ${station.locator}, ${station.power_dbm} dBm.${keep ? " Your draft is preserved; review before saving." : ""}`
                : "No saved station. Use the existing setup workflow first.";
        if (a.wspr_plan) member("station-feedback").textContent += ` Saved plan: ${a.wspr_plan.frame_types.join(" → ")}. ${a.wspr_plan.rationale}`;
        member("pairs").textContent = data.pin_choices.i2c_pairs.map(p => `Pair ${p.id}: I2C${p.controller} GP${p.sda_gp}/GP${p.scl_gp}`).join("; ");
        if (!keep) {
            if (station && !stationDirty && !progressLoaded) for (const [name,value] of Object.entries({callsign:station.callsign,locator:station.locator,power:station.power_dbm})) member(name).value = String(value);
            const options = (name, optional) => {
                member(name).innerHTML = `<option value="">${optional ? "Disabled" : "Select GP"}</option>` + data.pin_choices.eligible_gps.map(gp => `<option value="${gp}">GP${gp}</option>`).join("");
            };
            options("rf", false); options("button", true); options("amplifier", true); options("indicator-gp", false);
            if (h.saved) for (const [name,value] of Object.entries({rf:h.saved.rf_gp,button:h.saved.button_gp,amplifier:h.saved.amplifier_gp,"amplifier-polarity":h.saved.amplifier_active_high !== false,indicator:h.saved.indicator,"indicator-gp":h.saved.indicator_gp,polarity:h.saved.indicator_active_high !== false}))
                member(name).value = value === null ? "" : String(value);
            else for (const name of ["rf","button","amplifier","amplifier-polarity","indicator","indicator-gp","polarity"]) member(name).value = "";
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
        if (resourceReview && !root.confirm("Review current saved and active observations beside your preserved draft. Send one new explicit save using the fresh revision?")) return;
        resourceReview = false;
        if (station && member("power").value === "") { member("station-feedback").textContent = "Select reported power for a complete station save, or save the unset field as progress."; return; }
        const payload = station ? { callsign:member("callsign").value, locator:member("locator").value, power_dbm:Number(member("power").value) } : hardwareCandidate();
        return request(`member/${kind}`, "PUT", {contract:'Wsprry-Control/1',schema:station ? resources.application.station.schema : "hardware", target:resources.target, [station ? "station" : "pins"]:payload}, resourceRevision);
    }
    async function request(resource, method = "GET", body = null, revision = "") {
        if (busy || (resource === "member/management" ? !canSelect : !available)) return;
        busy = true; render();
        const shared = ["member/application", "member/station", "member/hardware", "member/pins/validate"].includes(resource);
        const feedback = shared ? member(resource.endsWith("validate") ? "pin-feedback" : method === "PUT" ? `${resource.split("/")[1]}-feedback` : "feedback")
            : byId(resource === "member/management" ? "wtp-management-connection-feedback" : "wtp-management-feedback");
        const context = resourceContext;
        if (method !== "GET" && ["member/configuration","member/station","member/hardware"].includes(resource)) {
            const intended = JSON.parse(JSON.stringify({...body,contract:"Wsprry-Control/1"}));
            if (intended.config?.wifi) intended.config.wifi.password = null;
            lastAttempt = {resource,method,revision,target:body.target,selectionId,intended,sent:true};
        }
        const recoveryAction = resource === "member/management" && ["inspect_logins","retire_login"].includes(body?.action);
        if (method !== "GET" && !resource.endsWith("validate")) invalidateEditors();
        feedback.textContent = "Contacting the idle Pico…";
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 180000);
        try {
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/" + resource, {
                method, cache: "no-store", signal: controller.signal,
                ...(body ? { headers: { "Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": revision, ...(shared ? { "X-WsprryPi-Member-Context": context } : {}) }, body: JSON.stringify({...body,contract:"Wsprry-Control/1"}) } : {})
            });
            const data = await response.json();
            if (!response.ok) {
                const code = data.error?.code;
                if (recoveryAction) {
                    const message = code === "consumer_recovery_unsupported" ? "This firmware has no authenticated login retirement. Existing reads and saves remain supported; occupied logins need their own logout or expiry."
                        : code === "radio_recovery_pending" ? "A retirement proof is pending for at most five seconds. No login was retired by this refusal."
                        : code === "radio_recovery_rate_limited" ? "Wait one second before another explicit retirement attempt."
                        : "Retirement is rejected or unconfirmed. Retain the exact selection for another explicit authenticated attempt. Read connection state; no save was replayed.";
                    if (byId("wtp-management-retirement-status")) byId("wtp-management-retirement-status").textContent = `${message} (${code || "response_unconfirmed"})`;
                    throw failure(message);
                }
                if (resource === "member/management") {
                    if (code === "management_session_already_selected") {
                        selectionObserved = false;
                        throw failure("A management session is already active. Read its connection state or disconnect it before selecting another connection.");
                    }
                    consumerConnected = false; explicitDisconnected = true; available = false;
                    throw failure(code === "radio_session_capacity"
                        ? "All three Pico radio sessions are in use. Inspect occupied logins and explicitly retire the affected login. Older firmware may require its own logout or expiry."
                        : code === "radio_login_pending"
                            ? "Another Pico login is in progress. Wait up to 30 seconds, then connect again."
                            : code === "radio_login_rate_limited"
                                ? "Pico logins must be at least one second apart. Wait a second, then connect again."
                                : code === "radio_session_busy"
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
                if (shared && code === "unsupported_station_encoding") throw failure("The complete station cannot be encoded by the selected Pico format. Check affixes, brackets and locator length. Your callsign and locator draft is preserved without truncation. Load resources before another save.");
                if (shared && code === "output_disable_failed") throw failure("The new settings may be saved, but output shutdown was not confirmed. Load resources to inspect saved and active plans. Restart/output inhibition remains latched; no restart was requested.");
                if (shared && code === "storage_fault") throw failure("Pico storage reported a fault. The save result is unconfirmed; load member resources to inspect trusted saved state. Your draft is preserved.");
                if (shared && method === "PUT" && data.mutation_attempted === true && response.status >= 500) throw failure("The save result is unconfirmed; new settings may already be stored. Your draft is preserved. Load member resources before another explicit save. No automatic replay occurred.");
                if (shared && ["unsupported_resource_capabilities","unsupported_pin_capabilities","invalid_remote_resource","invalid_remote_pins","invalid_pin_validation","unsupported_management_contract"].includes(code)) throw failure("This Pico's resources could not be validated safely. Your draft is preserved; load resources before saving. No automatic fallback was attempted.");
                throw failure(response.status === 412
                    ? "Pico settings changed elsewhere. Your draft is preserved. Load saved Pico settings before saving again."
                    : code === "unsupported_configuration_updates" || code === "invalid_remote_configuration"
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
                    if (data.schema !== "member-resources" || !data.application || !data.target || typeof data.member_context !== "string" || !etag)
                        throw failure("Member resources are incomplete. Your draft is preserved; load resources again.");
                    resourceReview = resourceReconcile && resourceLoaded;
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
            } else if (resource === "member/management" && recoveryAction) {
                if (body.action === "inspect_logins") {
                    if (data.schema !== "consumer-recovery/1" || !Array.isArray(data.sessions) || data.sessions.length > 3 || typeof data.selection_id !== "string") throw failure("Retirement inspection is unconfirmed.");
                    retirement = {...data,carrier:body.carrier,host:body.host}; selectionId = data.selection_id;
                    const select = byId("wtp-management-target");
                    select.replaceChildren(new Option("Choose a login explicitly",""),...data.sessions.map(s => new Option(`${s.selector} · ${s.state}${nativeSession?.selector === s.selector ? " · retained native selector" : ""}`,s.selector)));
                    byId("wtp-management-retirement-status").textContent = `Pico ${data.device_id}; ${data.interface} at ${data.authority}. A selected login may belong to another operator. Enter the current saved Wi-Fi password and explicitly retire one login.`;
                } else {
                    selectionObserved = false;
                    byId("wtp-management-retirement-status").textContent = "Selected login retired or already absent. Read connection state before explicitly connecting. Read saved settings before a new save. No replacement session or save was sent.";
                }
                feedback.textContent = "Login inspection/retirement does not establish a save outcome. Your drafts are preserved.";
            } else if (resource === "member/management") {
                managementCarrier = data.carrier || (body?.action === "connect" ? body.carrier : ["engineering_mtls","usb_physical","disconnect"].includes(body?.action) ? body.action : managementCarrier);
                consumerConnected = data.connected === true;
                if (method === "GET") {
                    if (!["engineering_mtls", "consumer_station", "consumer_ap", "usb_physical", "unsupported", "disconnected"].includes(data.carrier) || typeof data.connected !== "boolean" || typeof data.host !== "string")
                        throw failure("Management connection state could not be read. Select a connection explicitly.");
                    selectionId = data.selection_id; nativeSession = data.session || null;
                    explicitDisconnected = ["unsupported", "disconnected"].includes(data.carrier) || (data.carrier.startsWith("consumer_") && !data.connected);
                    if ((consumerConnected || data.carrier === "usb_physical") && byId("wtp-management-carrier")) {
                        byId("wtp-management-carrier").value = data.carrier;
                        byId("wtp-management-host").value = data.host;
                    }
                } else explicitDisconnected = body.action === "disconnect";
                available = canSelect && (consumerConnected || (!explicitDisconnected && ["network","usb"].includes(root.WtpUi?.read().Transport)));
                invalidateEditors();
                feedback.textContent = data.carrier === "usb_physical" || body?.action === "usb_physical"
                    ? "USB management selected. Load saved Pico settings before editing. Wi-Fi and a network password are not required."
                    : consumerConnected
                    ? "Consumer management authenticated for this Pico and boot. Load saved Pico settings before editing. The login password has been cleared."
                    : explicitDisconnected ? "Management disconnected. Only this client's consumer session was closed. Your settings draft is preserved."
                        : "Engineering HTTPS selected. Load saved Pico settings before editing.";
            } else if (resource === "network") {
                networkRevision = etag;
                const discovery = data.mdns_state ? ` Discovery: ${data.mdns_state}. Configured hostname: ${data.configured_hostname || "none"}. Advertised hostname: ${data.advertised_hostname || "none"}. Current address: ${data.ipv4 || "unavailable"}.` : "";
                const problem = data.mdns_state === "conflict" ? " Resolve the name conflict, then retry the same certified name with an idle disable/re-enable or restart. No automatic rename occurred."
                    : data.mdns_state === "failed" ? ` Discovery failed (${data.mdns_reason || "unconfirmed reason"}); inspect the Pico deployment before retrying.` : "";
                feedback.textContent = `Pico Wi-Fi: ${data.enabled ? "enabled" : "disabled"}.${discovery}${problem} ${method === "PUT" ? "Network control may now disconnect its network carrier. USB management stays available." : "This is a remote observation."}`;
                if (method === "PUT") { networkRevision = ""; configRevision = ""; reconcile = true; }
            } else {
                if (data.schema !== "pico-settings" || !data.target || !Object.hasOwn(data, "config") || !etag)
                    throw failure("Pico settings context is incomplete. Your draft is preserved; load saved settings again.");
                configRevision = etag; target = data.target;
                if (method === "GET") {
                    const keepDraft = reconcile && loaded;
                    configReview = keepDraft;
                    saved = data.config;
                    populate(saved, keepDraft);
                    loaded = true; reconcile = false;
                    feedback.textContent = keepDraft
                        ? "Saved Pico settings read. Your draft and password entry are preserved. Review the saved values before saving again."
                        : saved ? `Saved Pico settings loaded. Changes preserve settings outside the edited fields.`
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
            if (resource === "member/management" && !error.operatorMessage) selectionObserved = false;
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
        if (configReview && !root.confirm("Review the current saved values beside your preserved draft. Matching values do not prove an earlier save committed. Send one new explicit save with this fresh revision?")) return;
        configReview = false;
        try {
            const entries = schedules(field("schedules").value);
            let config = { schedules: entries };
            if (!onlySchedules) {
                const expiry = Number(field("expiry").value), power = Number(field("power").value);
                if (!Number.isSafeInteger(expiry) || expiry < 0 || expiry > 2147483647 || !Number.isInteger(power)) throw new Error("Enter whole-number power and expiry values; expiry must be at most 2147483647.");
                config = changes(saved, { enabled: field("enabled").checked,
                    station: { callsign: field("callsign").value, locator: field("locator").value, power_dbm: power },
                    wifi: { ssid: field("ssid").value, ntp_ipv4: field("ntp").value,
                        password: field("password").value || (saved ? null : "") },
                    schedules: entries, expires_utc_s: expiry });
                if (!Object.keys(config).length) { byId("wtp-management-feedback").textContent = "No Pico settings changed."; return; }
            }
            return request("member/configuration", "POST", { schema: "pico-settings", target,
                operation: onlySchedules ? "schedules" : "save", config }, configRevision);
        } catch (error) { byId("wtp-management-feedback").textContent = error.message; }
    }
    function controlValue(value) {
        if (!value || value.contract !== "Wsprry-Control/1") throw Error("The Pico control reply is incompatible. Update both applications and reload saved settings.");
        const result={...value}; delete result.contract; return result;
    }
    const filterEditor = root.WsprryFilters ? root.WsprryFilters.mount(byId('filter-selector-controls'), {
        pi:true,
        exchange:async(method,body,revision)=>{
            const r=await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1')+'/member/filter-selector',{
                method,cache:'no-store',signal:AbortSignal.timeout(20000),...(body?{headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','If-Match':revision},body:JSON.stringify({...body,contract:"Wsprry-Control/1"})}:{})});
            const data=await r.json();if(!r.ok){const e=Error('Selector request was refused or unconfirmed. Read saved selectors and verify the selected Pico.');e.code=data.error?.code;throw e;}
            return {data:controlValue(data),revision:r.headers.get('ETag')||''};
        },afterSave:()=>{configRevision='';resources=null;render();}
    }) : null;
    const timingEditor = root.WsprryAmplifierTiming && byId('amplifier-lead-ms') ? root.WsprryAmplifierTiming.create({
        lead:byId('amplifier-lead-ms'),tail:byId('amplifier-tail-ms'),read:byId('amplifier-timing-read'),save:byId('amplifier-timing-save'),discard:byId('amplifier-timing-discard'),adopt:byId('amplifier-timing-adopt'),status:byId('amplifier-timing-status'),
        exchange:async(method,body,revision)=>{
            const r=await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1')+'/member/amplifier-timing',{
                method,cache:'no-store',signal:AbortSignal.timeout(20000),...(body ? {headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','If-Match':revision},body:JSON.stringify({...body,contract:"Wsprry-Control/1"})} : {})});
            const data=await r.json();if(!r.ok)throw Error('Amplifier timing request was refused or unconfirmed. Read saved timing and verify the selected Pico.');
            return {data:controlValue(data),revision:r.headers.get('ETag') || ''};
        },afterSave:()=>{configRevision=''; resources=null;render();}
    }) : null;
    const driveEditor = root.WsprryGpioDrive && byId('gpio-drive-select') ? root.WsprryGpioDrive.create({
        select:byId('gpio-drive-select'),read:byId('gpio-drive-read'),save:byId('gpio-drive-save'),discard:byId('gpio-drive-discard'),status:byId('gpio-drive-status'),saveDraft:byId('gpio-drive-save-draft'),applyDraft:byId('gpio-drive-apply-draft'),draftStatus:byId('gpio-drive-draft-status'),
        exchange:async(method,body,revision,resource = 'gpio-drive')=>{
            if (resource === 'progress/update') method = 'PUT';
            const r = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || '/api/control/v1') + '/member/' + (resource === 'gpio-drive' ? 'gpio-drive' : 'gpio-drive-progress'),{
                method,cache:'no-store',signal:AbortSignal.timeout(20000),...(body ? {headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','If-Match':revision},body:JSON.stringify({...body,contract:"Wsprry-Control/1"})} : {})});
            const data = await r.json(); if (!r.ok) throw new Error(r.status === 409 ? 'Selected Pico does not support this setting or needs fresh identity/readback.' : 'Request was not confirmed.');
            return {data:controlValue(data),revision:r.headers.get('ETag') || ''};
        },afterSave:()=>invalidateEditors()
    }) : null;
    let driveSelection = "";
    root.WtpManagement = { invalidate:invalidateEditors, setAvailability(value, selectable = value) {
        const selectedDrive = root.WtpUi?.read?.() || {};
        const key = JSON.stringify(['Transport','Endpoint','USB Serial','Device ID','Hostname','TCP Port'].map(k=>selectedDrive[k]));
        if (driveSelection && key !== driveSelection) { invalidateEditors(); progressLoaded = false; }
        if (!value && !selectable) invalidateProgress();
        driveSelection = key;
        canSelect = selectable;
        available = !explicitDisconnected && (consumerConnected ? selectable : value);
        driveEditor?.setAvailability(available); timingEditor?.setAvailability(available); filterEditor?.setAvailability(available);
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
            invalidateEditors();
            retirement = null;
            if (byId("wtp-management-target")) byId("wtp-management-target").replaceChildren(new Option("Inspect this endpoint first",""));
            render();
        });
        byId("wtp-management-host")?.addEventListener("input", () => {
            invalidateEditors();
            retirement = null; byId("wtp-management-target")?.replaceChildren(new Option("Inspect this endpoint first",""));
            if (byId("wtp-management-password")) byId("wtp-management-password").value = "";
            render();
        });
        byId("wtp-management-connect")?.addEventListener("click", () => {
            const carrier = byId("wtp-management-carrier").value;
            return request("member/management", "POST", ["engineering_mtls","usb_physical"].includes(carrier)
                ? { action: carrier }
                : { action: "connect", carrier, host: byId("wtp-management-host").value,
                    password: byId("wtp-management-password").value });
        });
        byId("wtp-management-inspect")?.addEventListener("click", () => {
            invalidateEditors();
            retirement = null;
            return request("member/management","POST",{action:"inspect_logins",carrier:byId("wtp-management-carrier").value,host:byId("wtp-management-host").value});
        });
        byId("wtp-management-target")?.addEventListener("change",render);
        byId("wtp-management-retire")?.addEventListener("click", () => {
            const targetSession = byId("wtp-management-target").value;
            if (!retirement || !targetSession || !root.confirm(`Retire login ${targetSession} on Pico ${retirement.device_id} at ${retirement.host} (${retirement.carrier})? It may belong to another operator. Saved settings and radio jobs remain.`)) return;
            return request("member/management","POST",{action:"retire_login",carrier:retirement.carrier,host:retirement.host,
                password:byId("wtp-management-password").value,selection_id:selectionId,
                scope:{target_selector:targetSession,device_id:retirement.device_id,boot_id:retirement.boot_id,generation:retirement.generation}});
        });
        byId("wtp-management-state")?.addEventListener("click", () => request("member/management"));
        byId("wtp-management-disconnect")?.addEventListener("click", () => request("member/management", "POST", { action: "disconnect" }));
        for (const action of ["load","save","apply","discard"]) byId(`wtp-usb-progress-${action}`)?.addEventListener("click", () => stationProgress(action));
        member("load")?.addEventListener("click", () => request("member/application"));
        member("station-save")?.addEventListener("click", () => resourceSave("station"));
        member("hardware-save")?.addEventListener("click", () => resourceSave("hardware"));
        member("validate")?.addEventListener("click", () => request("member/pins/validate", "POST", hardwareCandidate(), resourceRevision));
        for (const name of ["callsign","locator","power"]) member(name)?.addEventListener("input", () => {
            ++stationEdits[name];
            stationDirty = true;
            if (!progressDraftTarget) progressDraftTarget = JSON.stringify(progress?.target || resources?.target || null);
            const call=member("callsign").value, grid=member("locator").value;
            member("station-feedback").textContent = call.startsWith("<") && grid.length !== 6 ? "A bracketed callsign needs a six-character locator. Your draft has not been saved." :
                "Draft plan: " + (call.startsWith("<") ? "Type 3, one hash frame; receiver context is needed." : call.includes("/") ?
                grid.length === 6 ? "Type 2 then Type 3, two frames in adjacent even-minute windows (230.592 seconds total); receiver hash context may be needed." :
                "Type 2, one frame without a locator." : "Type 1, one frame; first four locator characters on air, all six retained.") +
                (!call.startsWith("<") && /\/(?:[Q-Z]|0[0-9])$/.test(call) ? " Type 2 is ambiguous with another suffix; the saved call and Type 3 hash retain the requested identity." : "") + " The Pico validates encoding before saving.";
        });
        for (const name of ["rf", "button", "amplifier", "amplifier-polarity", "indicator", "indicator-gp", "polarity"]) member(name)?.addEventListener("change", () => {
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
