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
    let configRevision = "", networkRevision = "", saved = null, target = null;
    const field = name => byId(`wtp-remote-${name}`);
    const originalHints = new Map();
    function failure(message) { const error = new Error(message); error.operatorMessage = true; return error; }
    function render() {
        if (!byId("wtp-management")) return;
        byId("wtp-management").hidden = !root.WtpUi?.developmentControlsVisible || root.WtpUi?.read().Transport !== "network";
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
    async function request(resource, method = "GET", body = null, revision = "") {
        if (busy || !available) return;
        busy = true; render();
        const feedback = byId("wtp-management-feedback");
        feedback.textContent = "Contacting the idle Pico…";
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 180000);
        try {
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/" + resource, {
                method, cache: "no-store", signal: controller.signal,
                ...(body ? { headers: { "Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": revision }, body: JSON.stringify(body) } : {})
            });
            const data = await response.json();
            if (!response.ok) {
                const code = data.error?.code;
                throw failure(response.status === 412
                    ? "Pico settings changed elsewhere. Your draft is preserved. Load saved Pico settings before saving again."
                    : code === "unsupported_configuration_updates" || code === "unsupported_legacy_field" || code === "invalid_remote_configuration"
                        ? "This Pico's settings cannot be preserved safely. No automatic fallback was attempted. Your draft is preserved."
                        : ["host_busy_or_output_unresolved", "remote_busy_or_output_unresolved", "busy"].includes(code)
                            ? "Pico management is busy or output is unresolved. Finish or reconcile that work, then load saved settings. Your draft is preserved."
                        : ["target_mismatch", "configuration_context_changed", "member_identity_unavailable"].includes(code)
                            ? "The selected Pico or its boot context changed. Load saved settings before saving. Your draft is preserved."
                        : code === "consumer_setup_identity_required"
                            ? "Pico setup owns these station or Wi-Fi values. Use Pico setup to change them. Your draft is preserved."
                            : "The Pico request was not confirmed. Your draft is preserved. Load saved Pico settings and review them before saving again.");
            }
            const etag = response.headers.get("ETag") || "";
            if (resource === "network") {
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
            if (resource !== "network" || method !== "GET") { configRevision = ""; target = null; reconcile = loaded; }
            if (resource === "network") networkRevision = "";
            feedback.textContent = error.name === "AbortError"
                ? "Pico request timed out. Its result is unconfirmed. Your draft is preserved; read saved settings before retrying a change."
                : error.operatorMessage ? error.message
                    : "Pico request or response could not be confirmed. Your draft is preserved; load saved settings before saving again.";
        } finally { clearTimeout(timeout); busy = false; render(); }
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
    root.WtpManagement = { setAvailability(value) { available = value; render(); } };
    function init() {
        if (!byId("wtp-management")) return;
        for (const id of Object.keys(values(null))) originalHints.set(id, byId(`wtp-remote-${id}-hint`)?.textContent || "");
        byId("wtp-remote-load").addEventListener("click", () => request("member/configuration"));
        byId("wtp-network-load").addEventListener("click", () => request("network"));
        byId("wtp-remote-save").addEventListener("click", () => save(false));
        byId("wtp-schedules-save").addEventListener("click", () => save(true));
        byId("wtp-network-disable").addEventListener("click", () => request("network", "PUT", { enabled: false }, networkRevision));
        render();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(typeof window === "undefined" ? globalThis : window);
