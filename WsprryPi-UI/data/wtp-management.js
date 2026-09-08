// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    function schedules(text) {
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
    if (typeof module !== "undefined" && module.exports) module.exports = { schedules };
    if (!root.document) return;
    const byId = id => root.document.getElementById(id);
    let available = false, busy = false, configRevision = "", networkRevision = "", saved = null;
    const field = name => byId(`wtp-remote-${name}`);
    function render() {
        if (!byId("wtp-management")) return;
        byId("wtp-management").hidden = !root.WtpUi?.developmentControlsVisible || root.WtpUi?.read().Transport !== "network";
        byId("wtp-management").setAttribute("aria-busy", String(busy));
        root.document.querySelectorAll("[data-wtp-remote]").forEach(input => { input.disabled = busy || !available || !configRevision; });
        for (const id of ["wtp-remote-load", "wtp-network-load"]) byId(id).disabled = busy || !available;
        for (const id of ["wtp-remote-save", "wtp-schedules-save"]) byId(id).disabled = busy || !available || !configRevision;
        byId("wtp-network-disable").disabled = busy || !available || !networkRevision;
    }
    async function request(resource, method = "GET", body = null, revision = "") {
        if (busy || !available) return;
        busy = true; render();
        const feedback = byId("wtp-management-feedback");
        feedback.textContent = "Contacting the idle Pico…";
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 90000);
        try {
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/" + resource, {
                method, cache: "no-store", signal: controller.signal,
                ...(body ? { headers: { "Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": revision }, body: JSON.stringify(body) } : {})
            });
            const data = await response.json();
            if (!response.ok) throw new Error(response.status === 412
                ? "Pico settings changed elsewhere. Your draft is preserved. Load saved Pico settings before saving again."
                : `${data.error?.code || "Request failed"}. Your draft is preserved; check connection and host ownership.`);
            const etag = response.headers.get("ETag") || "";
            if (resource === "network") {
                networkRevision = etag;
                const discovery = data.mdns_state ? ` Discovery: ${data.mdns_state}. Configured hostname: ${data.configured_hostname || "none"}. Advertised hostname: ${data.advertised_hostname || "none"}. Current address: ${data.ipv4 || "unavailable"}.` : "";
                const problem = data.mdns_state === "conflict" ? " Resolve the name conflict, then retry the same certified name with an idle disable/re-enable or restart. No automatic rename occurred."
                    : data.mdns_state === "failed" ? ` Discovery failed (${data.mdns_reason || "unconfirmed reason"}); inspect the Pico deployment before retrying.` : "";
                feedback.textContent = `Pico Wi-Fi: ${data.enabled ? "enabled" : "disabled"}.${discovery}${problem} ${method === "PUT" ? "Network control may now disconnect; use USB Console or restart to reconnect." : "This is a remote observation."}`;
                if (method === "PUT") networkRevision = "";
            } else {
                configRevision = etag;
                if (method === "GET") {
                    saved = data.config;
                    const c = saved || { enabled: false, station: {}, wifi: {}, schedules: [{ period_s: 120, phase_s: 0 }] };
                    for (const [id, value] of Object.entries({ callsign: c.station.callsign || "", locator: c.station.locator || "", power: c.station.power_dbm ?? 20, ssid: c.wifi.ssid || "", ntp: c.wifi.ntp_ipv4 || "", expiry: c.expires_utc_s || 0, password: "", schedules: c.schedules.map(s => `${s.period_s} ${s.phase_s}`).join("\n") })) field(id).value = value;
                    field("enabled").checked = c.enabled === true;
                    feedback.textContent = saved ? "Saved Pico settings loaded." : "Pico has no saved standalone settings. Complete the fields before saving.";
                } else {
                    if (resource === "config") field("password").value = "";
                    if (data.config) saved = data.config;
                    feedback.textContent = (resource === "schedules" ? "Pico schedules saved. Other drafts are preserved." : "Pico settings saved.") + " A Pico restart is required to apply them; no restart was requested.";
                }
            }
        } catch (error) {
            feedback.textContent = error.name === "AbortError"
                ? "Pico request timed out. Its result is unconfirmed. Your draft is preserved; read saved settings before retrying a change."
                : error.message;
        } finally { clearTimeout(timeout); busy = false; render(); }
    }
    function save(onlySchedules) {
        try {
            const entries = schedules(field("schedules").value);
            if (onlySchedules) return request("schedules", "PUT", { schedules: entries }, configRevision);
            const expiry = Number(field("expiry").value), power = Number(field("power").value);
            if (!Number.isSafeInteger(expiry) || expiry < 0 || expiry >= 4102444800 || !Number.isInteger(power)) throw new Error("Enter whole-number power and expiry values.");
            const body = { version: 1, enabled: field("enabled").checked,
                station: { callsign: field("callsign").value, locator: field("locator").value, power_dbm: power },
                wifi: { ssid: field("ssid").value, ntp_ipv4: field("ntp").value,
                    password: field("password").value || (saved ? null : "") },
                schedules: entries, expires_utc_s: expiry };
            return request("config", "PUT", body, configRevision);
        } catch (error) { byId("wtp-management-feedback").textContent = error.message; }
    }
    root.WtpManagement = { setAvailability(value) { available = value; render(); } };
    function init() {
        if (!byId("wtp-management")) return;
        byId("wtp-remote-load").addEventListener("click", () => request("config"));
        byId("wtp-network-load").addEventListener("click", () => request("network"));
        byId("wtp-remote-save").addEventListener("click", () => save(false));
        byId("wtp-schedules-save").addEventListener("click", () => save(true));
        byId("wtp-network-disable").addEventListener("click", () => request("network", "PUT", { enabled: false }, networkRevision));
        render();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(typeof window === "undefined" ? globalThis : window);
