// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    const defaults = Object.freeze({
        "Transport": "usb", "Hostname": "", "TCP Port": 0, "TLS Server Identity": "",
        "TLS CA File": "", "TLS Client Certificate": "", "TLS Client Key": "",
        "Endpoint": "", "USB Serial": "", "Device ID": "",
        "USB Vendor ID": 0, "USB Product ID": 0,
        "Start Uncertainty ns": 1000000, "Allow Frequency Adjustment": false
    });
    function validNetworkIdentity(value) {
        if (typeof value !== "string" || !value || value.length > 254 || /[^\x21-\x7e]/.test(value)) return false;
        const ipv4 = v => /^(0|[1-9][0-9]{0,2})(\.(0|[1-9][0-9]{0,2})){3}$/.test(v) && v.split(".").every(n => Number(n) <= 255);
        if (ipv4(value)) return true;
        if (value.includes(":")) {
            if (!/^[0-9a-fA-F:.]+$/.test(value) || value.split("::").length > 2 || (value.startsWith(":") && !value.startsWith("::")) || (value.endsWith(":") && !value.endsWith("::"))) return false;
            const groups = value.split(":").filter(Boolean);
            let count = 0;
            for (let i = 0; i < groups.length; ++i) {
                if (groups[i].includes(".")) {
                    if (i !== groups.length - 1 || !value.endsWith(groups[i]) || !ipv4(groups[i])) return false;
                    count += 2;
                } else { if (!/^[0-9a-fA-F]{1,4}$/.test(groups[i])) return false; ++count; }
            }
            return value.includes("::") ? count < 8 && !value.includes(":::")
                : count === 8 && !value.startsWith(":") && !value.endsWith(":");
        }
        const name = value.endsWith(".") ? value.slice(0, -1) : value;
        if (/^[0-9.]+$/.test(name) || name.split(".").every(label => /^(?:[0-9]+|0x[0-9a-f]+)$/i.test(label))) return false;
        return name.length <= 253 && name.split(".").every(label => /^[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$/.test(label));
    }
    function errors(settings, selected) {
        const result = {};
        for (const [key, max] of [["Endpoint", 512], ["USB Serial", 128], ["Device ID", 32]]) {
            const value = settings[key];
            if (typeof value !== "string" || value.length > max || /[\x00-\x1f\x7f]/.test(value))
                result[key] = `Enter a valid ${key.toLowerCase()}.`;
        }
        if (!["usb", "network"].includes(settings.Transport)) result.Transport = "Choose USB or network.";
        const network = settings.Transport === "network";
        for (const key of ["Hostname", "TLS Server Identity"]) {
            if (settings[key] !== "" && !validNetworkIdentity(settings[key]))
                result[key] = "Enter a DNS hostname or literal IP address, without a URL or port.";
        }
        for (const key of ["TLS CA File", "TLS Client Certificate", "TLS Client Key"]) {
            const value = settings[key];
            if (typeof value !== "string" || value.length > 512 || /[\x00-\x1f\x7f]/.test(value)) result[key] = "Enter a valid local file reference.";
            else if (selected && network && !value.startsWith("/")) result[key] = "Enter an absolute file path on the WsprryPi host.";
        }
        if (selected && network && !settings.Hostname) result.Hostname = "Enter the Pico's configured hostname or literal IP address.";
        if (!Number.isInteger(settings["TCP Port"]) || settings["TCP Port"] < (selected && network ? 1 : 0) || settings["TCP Port"] > 65535) result["TCP Port"] = "Enter the configured TLS port (1–65535).";
        if (selected) {
            if (!network && !String(settings.Endpoint).startsWith("/dev/") || (!network && settings.Endpoint.includes("/../"))) result.Endpoint = "Select the dedicated WTP device path under /dev/.";
            if (!network && !settings["USB Serial"]) result["USB Serial"] = "Enter the selected device's USB serial.";
            if (!/^[0-9a-f]{32}$/.test(settings["Device ID"])) result["Device ID"] = "Enter 32 lowercase hexadecimal characters.";
        }
        for (const key of ["USB Vendor ID", "USB Product ID", "Start Uncertainty ns"]) {
            const value = settings[key];
            const minimum = key === "Start Uncertainty ns" || (selected && !network) ? 1 : 0;
            const maximum = key === "Start Uncertainty ns" ? 1000000000 : 65535;
            if (!Number.isInteger(value) || value < minimum || value > maximum) result[key] = `Enter a whole number from ${minimum} to ${maximum}.`;
        }
        return result;
    }
    function summarize(s) {
        if (!s || s.selected !== true) return { text: s?.selected === false ? "Pico is not selected in the running application." : "Pico status is unavailable. Selection and output state are unconfirmed.", output: "Unknown", clock: "Unknown", identity: "Unknown", history: "None", recover: false };
        let age = null;
        if (/^\d+$/.test(s.now_ms) && /^\d+$/.test(s.status_observed_ms)) {
            const delta = BigInt(s.now_ms) - BigInt(s.status_observed_ms);
            if (delta >= 0n) age = `${delta / 1000n} s ago`;
        }
        const remote = s.remote;
        const output = typeof remote?.output_active === "boolean" && age !== null
            ? `${remote.output_active ? "Active" : "Inactive"} (${age}; last observation)` : "Unknown";
        const blocked = s.recovery_required || s.uncertain || s.safety_fault;
        const text = blocked ? "Recovery required. Output safety remains unresolved."
            : s.host_skip_waiting ? "Waiting for a skipped WSPR window; no Pico job was sent."
            : s.phase === "waiting" ? "Waiting to prepare the scheduled job."
            : s.phase === "preparing" ? "Preparing the complete job."
            : s.phase === "executing" ? `Device job: ${s.job?.state || "unconfirmed"}.`
            : s.ready ? "Idle. Device state was checked; each job requires fresh admission."
            : "Device readiness is unconfirmed. Check the endpoint and reconcile.";
        return { text, output, clock: s.host_utc_valid === true ? "Synchronized" : "Not synchronized",
            identity: s.identity ? `${s.identity.device_id} / ${s.identity.boot_id}` : "Unknown",
            history: s.last_report ? `${s.last_report.outcome} · ${s.last_report.job_id || "no remote job"}${s.last_report.error ? ` · ${s.last_report.error}` : ""}` : "None",
            recover: !s.worker_active && ["idle", "blocked"].includes(s.phase) && !["identity_changed", "fault"].includes(s.session_phase) };
    }
    if (typeof module !== "undefined" && module.exports) module.exports = { defaults, errors, summarize, validNetworkIdentity };
    if (!root.document) return;
    const byId = id => root.document.getElementById(id);
    let saved = { ...defaults }, snapshot = null, busy = false, timer = null, initialized = false, visible = false, closed = false, statusReadFailed = false, hostRevision = "", cancelling = false;
    let browserSession = "";
    const selected = () => byId("wtp_use")?.checked === true;
    function read() {
        const result = { ...saved };
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => {
            result[field.dataset.wtpKey] = field.type === "checkbox" ? field.checked
                : field.type === "number" ? (field.value === "" ? NaN : Number(field.value)) : field.value;
        });
        return result;
    }
    function validate() {
        const invalid = errors(read(), selected());
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => field.setCustomValidity(invalid[field.dataset.wtpKey] || ""));
        return Object.keys(invalid).length === 0;
    }
    function render() {
        if (!byId("wtp-controls")) return;
        byId("wtp-controls").hidden = !visible;
        byId("wtp-hidden-selection").hidden = visible || !selected();
        byId("wtp-development").hidden = !visible && !selected();
        byId("wtp_use").disabled = !visible;
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => { field.disabled = !visible || !selected(); });
        const network = read().Transport === "network";
        root.document.querySelectorAll("[data-wtp-transport]").forEach(group => { group.hidden = group.dataset.wtpTransport !== (network ? "network" : "usb"); });
        const n = snapshot?.network;
        if (byId("wtp-network-state")) byId("wtp-network-state").textContent = n
            ? `Configured target: ${n.hostname}:${n.port} · ${n.state}. Expected identity: ${n.expected_identity || "unconfirmed"}. Resolved address: ${n.resolved_address || "unresolved"}. Last authenticated identity: ${n.authenticated_identity || "unconfirmed"}. Observation age: ${n.observed_ms && snapshot.now_ms ? Math.max(0, Number(BigInt(snapshot.now_ms) - BigInt(n.observed_ms))) + " ms" : "unknown"}. ${n.diagnostic || ""}`
            : "Network connection is unconfirmed.";
        root.WtpManagement?.setAvailability(visible && selected() && network && snapshot?.selected === true && snapshot.ready === true && snapshot.phase === "idle" && !snapshot.worker_active && !snapshot.recovery_required && !snapshot.owns);
        const state = summarize(snapshot);
        for (const [id, value] of [["wtp-status-text", state.text], ["wtp-output", state.output], ["wtp-clock", state.clock], ["wtp-identity", state.identity], ["wtp-history", state.history]]) byId(id).textContent = value;
        if (byId("wtp-cancel")) byId("wtp-cancel").disabled = cancelling || !selected() || snapshot?.selected !== true || !snapshot?.job_id || !(snapshot.owns || snapshot.phase === "waiting");
        byId("wtp-recover").disabled = busy || !selected() || !state.recover;
        if (selected() && typeof root.updateBackendPlatformSupportUi === "function") root.updateBackendPlatformSupportUi();
        else if (typeof root.syncTransmitAvailabilityUi === "function") root.syncTransmitAvailabilityUi();
    }
    async function request(recover = false) {
        if (busy || closed || (!visible && !selected())) return;
        busy = true;
        clearTimeout(timer);
        render();
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), recover ? 30000 : 5000);
        if (recover) byId("wtp-feedback").textContent = "Reconciling the current session…";
        let receivedStatus = false;
        try {
            const url = recover ? (root.WSPRRYPI_PATHS?.wtpPath || "/api/wtp") + "/recover"
                : (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/status";
            const response = await root.fetch(url, {
                method: recover ? "POST" : "GET", cache: "no-store", signal: controller.signal,
                ...(recover ? { headers: { "Content-Type": "application/json" }, body: JSON.stringify({ operation: "reconcile" }) } : {})
            });
            const data = await response.json();
            if (recover && data.status) { snapshot = data.status; receivedStatus = true; }
            if (!response.ok) throw new Error(data.error || `Request failed (${response.status}).`);
            if (!recover) {
                snapshot = data.host || data;
                if (statusReadFailed) byId("wtp-feedback").textContent = "Status connection restored.";
                statusReadFailed = false;
            }
            else byId("wtp-feedback").textContent = "Reconciliation finished. Review the current observation above.";
        } catch (error) {
            // Historical results may remain, but a failed read cannot establish current safety.
            if (!recover) statusReadFailed = true;
            if (!receivedStatus) snapshot = null;
            byId("wtp-feedback").textContent = error.name === "AbortError"
                ? "The request timed out. Device state is unconfirmed; refresh status before another recovery."
                : `Pico status unavailable: ${error.message}`;
        } finally {
            clearTimeout(timeout);
            busy = false;
            render();
            if (!closed && (visible || selected())) timer = setTimeout(() => request(), 3000);
        }
    }
    function populate(value) {
        saved = { ...defaults, ...(value || {}) };
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => {
            const value = saved[field.dataset.wtpKey];
            if (field.type === "checkbox") field.checked = value === true;
            else field.value = String(value);
        });
        render();
    }
    root.WtpUi = { selected, read, validate, populate,
        get hostRevision() { return hostRevision; },
        setHostRevision(value) { if (typeof value === "string" && value) hostRevision = value; },
        get developmentControlsVisible() { return visible; },
        set developmentControlsVisible(value) {
            if (typeof value !== "boolean") throw new TypeError("developmentControlsVisible must be a boolean.");
            visible = value;
            if (!initialized) return;
            render();
            if (visible || selected()) request(); else clearTimeout(timer);
        },
        select(value) { if (byId("wtp_use")) byId("wtp_use").checked = value; render(); if (initialized && !busy) request(); },
        unavailable() {
            if (!selected()) return "";
            if (!validate()) return "Complete the Pico endpoint settings before enabling transmission.";
            return snapshot?.selected === true && snapshot.ready === true && snapshot.host_utc_valid === true
                ? "" : "Pico and host UTC readiness are unconfirmed. Review Pico status before enabling transmission.";
        }
    };
    async function cancelJob() {
        const job = snapshot?.job_id;
        if (byId("wtp-cancel").disabled || !job) return;
        cancelling = true; render();
        const feedback = byId("wtp-feedback");
        feedback.textContent = "Cancelling the current Pico job…";
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 45000);
        try {
            if (!browserSession) browserSession = Array.from(root.crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, "0")).join("");
            const requestId = Array.from(root.crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, "0")).join("");
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/jobs", {
                method: "POST", cache: "no-store", signal: controller.signal,
                headers: { "Content-Type": "application/json", "X-WsprryPico-Request": "1" },
                body: JSON.stringify({session_id: browserSession, request_id: requestId, operation: "ABORT", body: {job_id: job}})
            });
            const result = await response.json();
            if (!response.ok || !result.ok) throw new Error(result.error?.code || "Cancellation was not confirmed");
            feedback.textContent = "Cleanup confirmed. Review the job outcome and output observation above.";
        } catch (error) {
            snapshot = null;
            feedback.textContent = `Cancellation is unconfirmed: ${error.message}. Review status and reconcile before further work.`;
        } finally { clearTimeout(timeout); cancelling = false; render(); request(); }
    }
    function init() {
        if (!byId("wtp-controls")) return;
        initialized = true;
        populate(saved);
        byId("wtp_use").addEventListener("change", () => { render(); root.clickTransmitBackend?.(); request(); });
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => field.addEventListener("change", () => { render(); validate(); }));
        byId("wtp-cancel")?.addEventListener("click", cancelJob);
        byId("wtp-recover").addEventListener("click", () => request(true));
        root.addEventListener("pagehide", () => { closed = true; clearTimeout(timer); });
        root.addEventListener("pageshow", () => { closed = false; request(); });
        render();
        if (visible || selected()) request();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init);
    else init();
})(typeof window === "undefined" ? globalThis : window);
