// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    if (!root.document) return;
    const byId = id => root.document.getElementById(`wtp-usb-${id}`);
    const api = resource => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/member/" + resource;
    let available = false, busy = false, hostname = null, wifi = null, nameRevision = "", wifiRevision = "", dirty = false, uncertain = false, reviewRequired = false;
    function render() {
        const section = byId("settings");
        if (!section) return;
        section.hidden = root.WtpUi?.read().Transport !== "usb";
        section.setAttribute("aria-busy", String(busy));
        byId("load").disabled = busy || !available;
        for (const field of ["hostname", "ssid", "password", "time-server"]) byId(field).disabled = busy || !available || !hostname || !wifi;
        byId("name-save").disabled = busy || !available || !nameRevision || hostname?.writable !== true;
        byId("wifi-save").disabled = busy || !available || !wifiRevision;
        byId("wifi-remove").disabled = busy || !available || !wifiRevision || !wifi?.wifi;
        if (byId("logging-read")) byId("logging-read").disabled = busy || !available || loggingBusy;
        for (const id of ["logging-save","logging-enabled","logging-severity"])
            if (byId(id)) byId(id).disabled = busy || !available || loggingBusy || !logging;
        for (const action of ["restart", "ap"]) byId(action).disabled = busy || !available || !nameRevision;
    }
    function invalidate() { nameRevision = wifiRevision = ""; loggingEpoch += 1; logging = null; if (byId("logging-save")) byId("logging-save").disabled = true; }
    async function exchange(resource, method = "GET", body, revision) {
        const response = await root.fetch(api(resource), {
            method, cache: "no-store", signal: AbortSignal.timeout(12000),
            ...(body ? { headers: {"Content-Type":"application/json", "X-WsprryPico-Request":"1", "If-Match":revision}, body:JSON.stringify(body) } : {})
        });
        const value = await response.json();
        if (!response.ok && resource === "logging") throw new Error(response.status === 404 ? "This firmware does not support USB logging. Update it, then read logging settings."
            : response.status === 412 ? "Logging settings changed elsewhere. Read logging settings and review your draft before another save."
            : response.status === 409 ? "USB management is unavailable or needs a fresh read. Select USB cable, reconcile any job, then read logging settings."
            : "The logging request was not confirmed. Read logging settings and review saved state before another save; your draft is preserved.");
        if (!response.ok) throw new Error(response.status === 412 ? "Settings changed elsewhere. Read name and Wi-Fi, review your preserved draft, then save again."
            : response.status === 409 ? "USB management is unavailable or needs a fresh read. Select USB cable above, reconcile any job, then read name and Wi-Fi."
            : "The Pico request was not confirmed. A change may already be saved. Read name and Wi-Fi before sending another change; your draft is preserved.");
        return {value, revision:response.headers.get("ETag") || ""};
    }
    let logging = null, loggingRevision = "", loggingBusy = false, loggingEpoch = 0;
    async function loggingAction(save) {
        if (loggingBusy || !available || save && !logging) return;
        loggingBusy = true;
        const controls = ["logging-read","logging-save","logging-enabled","logging-severity"];
        for (const id of controls) if (byId(id)) byId(id).disabled = true;
        const expected = logging, epoch = loggingEpoch; logging = null;
        try {
            if (save) await exchange("logging","PUT",{contract:'Wsprry-Control/1',schema:"logging",target:expected.target,
                logging:{enabled:byId("logging-enabled").checked,maximum_severity:Number(byId("logging-severity").value)}},loggingRevision);
            const observed = await exchange("logging");
            if (epoch !== loggingEpoch || !available) return;
            const value = observed.value;
            if (value.schema !== "logging" || value.target?.scope !== "member" ||
                !/^[0-9a-f]{32}$/.test(value.target?.device_id || "") || !/^[0-9a-f]{32}$/.test(value.target?.boot_id || "") ||
                typeof value.saved?.enabled !== "boolean" || !Number.isInteger(value.saved?.maximum_severity) ||
                value.saved.maximum_severity < 0 || value.saved.maximum_severity > 7 ||
                typeof value.active?.enabled !== "boolean" || typeof value.active?.streaming !== "boolean" ||
                !/^(0|[1-9][0-9]{0,19})$/.test(value.active?.dropped || "") || !observed.revision)
                throw new Error("This firmware does not support USB logging settings.");
            logging = observed.value; loggingRevision = observed.revision;
            byId("logging-enabled").checked = logging.saved.enabled;
            byId("logging-severity").value = String(logging.saved.maximum_severity);
            byId("logging-status").textContent = `Saved: ${logging.saved.enabled ? "enabled" : "disabled"}. Active: ${logging.active.enabled ? "enabled" : "disabled"}; ${logging.active.streaming ? "stream started" : "stream stopped"}; ${logging.active.dropped} dropped events. Enable Pi collection on Logs.`;
            for (const id of controls) if (byId(id)) byId(id).disabled = false;
        } catch (error) {
            if (epoch !== loggingEpoch || !available) return;
            byId("logging-status").textContent = (save ? "Save unconfirmed. Read logging settings before another save. " : "") + error.message;
        } finally { loggingBusy = false; render(); }
    }
    byId("logging-read")?.addEventListener("click", () => loggingAction(false));
    byId("logging-save")?.addEventListener("click", () => loggingAction(true));
    async function perform(action) {
        if (busy || !available) return;
        busy = true; render();
        try {
            if (action === "load") {
                const name = await exchange("hostname"), network = await exchange("wifi");
                if (name.value.schema !== "hostname" || network.value.schema !== "wifi" ||
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
                const payload = action === "name-save" ? {contract:'Wsprry-Control/1',schema:"hostname",target,hostname:byId("hostname").value || null}
                    : action === "wifi-save" ? {contract:'Wsprry-Control/1',schema:"wifi",target,wifi:{ssid:byId("ssid").value,password:byId("password").value || (wifi?.wifi ? null : ""),time_server:byId("time-server").value}}
                    : action === "wifi-remove" ? {contract:'Wsprry-Control/1',schema:"wifi",target,wifi:null}
                    : action === "ap" ? {contract:'Wsprry-Control/1',schema:"ap-request",target} : {};
                const revision = action.startsWith("wifi-") ? wifiRevision : nameRevision;
                invalidate(); root.WtpManagement?.invalidate(); uncertain = true;
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
    root.WtpUsbSettings = {invalidate() { invalidate(); render(); }, setAvailability(value) { if (!value) invalidate(); available = value; render(); }};
    function init() {
        if (!byId("settings")) return;
        for (const action of ["load", "name-save", "wifi-save", "wifi-remove", "restart", "ap"])
            byId(action).addEventListener("click", () => perform(action));
        for (const field of ["hostname", "ssid", "password", "time-server"])
            byId(field).addEventListener("input", () => { dirty = true; });
        render();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(typeof window === "undefined" ? globalThis : window);
