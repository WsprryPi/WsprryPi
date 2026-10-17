// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    if (!root.document) return;
    const byId = id => root.document.getElementById(`wtp-usb-${id}`);
    const api = resource => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/member/" + resource;
    let progress = null, progressRevision = "", stationEditorReady = false;
    let available = false, busy = false, hostname = null, wifi = null, nameRevision = "", wifiRevision = "", dirty = false, uncertain = false, reviewRequired = false;
    function render() {
        const section = byId("settings");
        if (!section) return;
        section.hidden = root.WtpUi?.read().Transport !== "usb";
        section.setAttribute("aria-busy", String(busy));
        byId("load").disabled = busy || !available;
        byId("progress-load").disabled = busy || !available;
        byId("progress-save").disabled = busy || !available || !progressRevision || !stationEditorReady;
        byId("progress-apply").disabled = busy || !available || !progressRevision || progress?.apply_domains?.station !== true;
        for (const field of ["hostname", "ssid", "password", "time-server"]) byId(field).disabled = busy || !available || !hostname || !wifi;
        byId("name-save").disabled = busy || !available || !nameRevision || hostname?.writable !== true;
        byId("wifi-save").disabled = busy || !available || !wifiRevision;
        byId("wifi-remove").disabled = busy || !available || !wifiRevision || !wifi?.wifi;
        for (const action of ["restart", "ap"]) byId(action).disabled = busy || !available || !nameRevision;
    }
    function invalidate() { nameRevision = wifiRevision = progressRevision = ""; }
    async function exchange(resource, method = "GET", body, revision) {
        const response = await root.fetch(api(resource), {
            method, cache: "no-store", signal: AbortSignal.timeout(12000),
            ...(body ? { headers: {"Content-Type":"application/json", "X-WsprryPico-Request":"1", "If-Match":revision}, body:JSON.stringify(body) } : {})
        });
        const value = await response.json();
        if (!response.ok) throw new Error(response.status === 412 ? "Settings changed elsewhere. Read name and Wi-Fi, review your preserved draft, then save again."
            : response.status === 409 ? "USB management is unavailable or needs a fresh read. Select USB cable above, reconcile any job, then read name and Wi-Fi."
            : "The Pico request was not confirmed. A change may already be saved. Read name and Wi-Fi before sending another change; your draft is preserved.");
        return {value, revision:response.headers.get("ETag") || ""};
    }
    async function perform(action) {
        if (busy || !available) return;
        busy = true; render();
        try {
            if (action === "progress-load") {
                const result = await exchange("progress");
                if (result.value.schema !== "transmitter-progress/1" || !result.revision || !result.value.target ||
                    (hostname && (result.value.target.device_id !== hostname.target.device_id || result.value.target.boot_id !== hostname.target.boot_id)))
                    throw new Error("Saved progress could not be confirmed. Read it again.");
                reviewRequired = reviewRequired || uncertain; uncertain = false;
                progress = result.value; progressRevision = result.revision;
                const station = progress.station || {};
                byId("progress-state").textContent = `Saved station: ${station.callsign ?? "unset"}, ${station.locator ?? "unset"}, ${station.power_dbm ?? "unset"} dBm. Draft ${progress.draft_revision}; station ${progress.station_complete ? "complete" : "incomplete"}; full configuration ${progress.complete ? "complete" : "incomplete"}; ${progress.matches_applied ? "matches applied settings" : "differs from applied settings"}. STOP ${progress.schedule_suspended ? "suspended" : "not suspended"}. This does not establish transmit readiness.`;
                byId("feedback").textContent = "Saved progress read. Load member resources below to edit station fields, then save valid fields or explicitly apply the saved station.";
            } else if (action === "progress-save" || action === "progress-apply") {
                if (reviewRequired && !root.confirm("Review saved progress and your draft. Send one new explicit change using this fresh read?")) return;
                reviewRequired = false;
                const revision = progressRevision, target = progress.target;
                let body;
                if (action === "progress-save") {
                    const field = name => root.document.getElementById(`wtp-member-${name}`);
                    if (!field("callsign") || field("callsign").disabled) throw new Error("Load member resources below before editing station progress. Read saved progress again if its revision changes.");
                    const power = field("power").value;
                    body = {schema:"transmitter-progress/1",target,request_id:Array.from(root.crypto.getRandomValues(new Uint8Array(16)), b=>b.toString(16).padStart(2,"0")).join(""),
                        fields:{callsign:field("callsign").value || null,locator:field("locator").value || null,power_dbm:power === "" ? null : Number.isSafeInteger(Number(power)) ? Number(power) : power}};
                } else body = {schema:"transmitter-progress/1",target,application:{draft_revision:progress.draft_revision,domain:"station"}};
                invalidate(); uncertain = true;
                const result = await exchange(action === "progress-save" ? "progress/update" : "progress/apply","POST",body,revision);
                uncertain = false;
                byId("progress-state").textContent = action === "progress-save" ? `Station progress saved. Field outcomes: ${JSON.stringify(result.value.outcomes || {})}. Read saved progress to inspect what is complete and applied.`
                    : "Saved station progress applied. Read progress and member resources to inspect applied settings. No transmission or STOP resume was requested.";
                byId("feedback").textContent = "Read saved progress and current resources before another change. Invalid fields remain in your station draft.";
            } else if (action === "load") {
                const name = await exchange("hostname"), network = await exchange("wifi");
                if (name.value.schema !== "transmitter-hostname/1" || network.value.schema !== "transmitter-wifi/1" ||
                    !name.revision || !network.revision || !name.value.target || !network.value.target ||
                    name.value.target.scope !== "member" || name.value.target.device_id !== network.value.target.device_id || name.value.target.boot_id !== network.value.target.boot_id)
                    throw new Error("Name and Wi-Fi identity could not be confirmed. Your draft is preserved; read again.");
                reviewRequired = reviewRequired || uncertain; uncertain = false;
                hostname = name.value; wifi = network.value; nameRevision = name.revision; wifiRevision = network.revision;
                byId("identity").textContent = `${hostname.saved} · USB · device ${hostname.target.device_id} · boot ${hostname.target.boot_id}`;
                byId("name-state").textContent = `Saved: ${hostname.saved}. Active: ${hostname.active}. Default: ${hostname.default}.${hostname.pending_restart ? " Name pending restart." : ""}${!hostname.writable ? " This deployment's name is protected by its credentials." : ""}`;
                if (!dirty) {
                    byId("hostname").value = hostname.saved;
                    byId("ssid").value = wifi.wifi?.ssid || "";
                    byId("time-server").value = wifi.wifi?.time_server || "pool.ntp.org";
                    byId("password").value = "";
                }
                byId("feedback").textContent = `${wifi.wifi ? "Saved Wi-Fi connection read: " + wifi.wifi.ssid + "." : "No saved Wi-Fi connection. USB control is available."}${wifi.pending_restart ? " Network changes pending restart." : ""}${dirty ? " Your draft is preserved; compare it with the saved name and current connection before saving." : ""}`;
            } else {
                if (reviewRequired && !root.confirm("Review the saved name and Wi-Fi beside your preserved draft. The previous change may already be saved. Send one new explicit change using this fresh read?")) return;
                reviewRequired = false;
                if (action === "wifi-save" && !byId("password").value && wifi?.wifi?.ssid !== byId("ssid").value)
                    throw new Error("Enter the password for this new or renamed Wi-Fi network. Your draft is preserved.");
                const target = hostname?.target;
                const payload = action === "name-save" ? {schema:"transmitter-hostname/1",target,hostname:byId("hostname").value || null}
                    : action === "wifi-save" ? {schema:"transmitter-wifi/1",target,wifi:{ssid:byId("ssid").value,password:byId("password").value || (wifi?.wifi ? null : ""),time_server:byId("time-server").value}}
                    : action === "wifi-remove" ? {schema:"transmitter-wifi/1",target,wifi:null}
                    : action === "ap" ? {schema:"transmitter-ap-request/1",target} : {};
                const revision = action.startsWith("wifi-") ? wifiRevision : nameRevision;
                invalidate(); uncertain = true;
                await exchange(action === "name-save" ? "hostname" : action.startsWith("wifi-") ? "wifi" : action,
                    action === "wifi-remove" ? "DELETE" : ["restart","ap"].includes(action) ? "POST" : "PUT", payload, revision);
                if (action === "wifi-save") byId("password").value = "";
                dirty = false; uncertain = false;
                byId("feedback").textContent = action === "ap" ? "A bounded setup Wi-Fi window was requested. An already active window is not extended. USB remains available."
                    : action === "restart" ? "Pico restart requested. Reconnect and reconcile after it reappears; no transmission was requested."
                    : action === "wifi-remove" ? "Saved Wi-Fi removed. USB control, common name and transmitter settings are retained. Read back before another change."
                    : "Settings saved. Read name and Wi-Fi to verify the saved values and any pending restart. No transmission was requested.";
            }
        } catch (error) {
            if (uncertain) dirty = true;
            invalidate();
            byId("feedback").textContent = error.message || "Request unconfirmed. Read back before another change; your draft is preserved.";
        } finally { busy = false; render(); }
    }
    root.WtpUsbSettings = {setAvailability(value) { available = value; render(); }, stationEditorReady(value) { stationEditorReady=value; render(); }};
    function init() {
        if (!byId("settings")) return;
        for (const action of ["load", "name-save", "wifi-save", "wifi-remove", "restart", "ap", "progress-load", "progress-save", "progress-apply"])
            byId(action).addEventListener("click", () => perform(action));
        for (const field of ["hostname", "ssid", "password", "time-server"])
            byId(field).addEventListener("input", () => { dirty = true; });
        render();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(typeof window === "undefined" ? globalThis : window);
