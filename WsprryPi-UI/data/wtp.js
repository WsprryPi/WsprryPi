// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    const defaults = Object.freeze({
        "Transport": "usb", "Hostname": "", "TCP Port": 31417, "TLS Server Identity": "",
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
        if (!["usb", "network_plain", "network"].includes(settings.Transport)) result.Transport = "Choose USB, Plain LAN or TLS.";
        const network = settings.Transport === "network" || settings.Transport === "network_plain";
        const tls = settings.Transport === "network";
        for (const key of ["Hostname", ...(tls ? ["TLS Server Identity"] : [])]) {
            if (settings[key] !== "" && !validNetworkIdentity(settings[key]))
                result[key] = "Enter a DNS hostname or literal IP address, without a URL or port.";
        }
        for (const key of ["TLS CA File", "TLS Client Certificate", "TLS Client Key"]) {
            const value = settings[key];
            if (typeof value !== "string" || value.length > 512 || /[\x00-\x1f\x7f]/.test(value)) result[key] = "Enter a valid local file reference.";
            else if (selected && tls && !value.startsWith("/")) result[key] = "Enter an absolute file path on the WsprryPi host.";
        }
        if (selected && network && !settings.Hostname) result.Hostname = "Enter the Pico's configured hostname or literal IP address.";
        if (!Number.isInteger(settings["TCP Port"]) || settings["TCP Port"] < (selected && network ? 1 : 0) || settings["TCP Port"] > 65535) result["TCP Port"] = "Enter the configured WTP port (1–65535).";
        if (selected) {
            if (!network && !String(settings.Endpoint).startsWith("/dev/") || (!network && settings.Endpoint.includes("/../"))) result.Endpoint = "Select the dedicated WTP device path under /dev/.";
            if (!network && !settings["USB Serial"]) result["USB Serial"] = "Enter the selected device's USB serial.";
            if (!(settings.Transport === "network_plain" && settings["Device ID"] === "") &&
                !/^[0-9a-f]{32}$/.test(settings["Device ID"]))
                result["Device ID"] = "Enter 32 lowercase hexadecimal characters.";
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
        // Fleet is a session-only feature flag and starts hidden on every load.
        const fleetTab = byId("fleet-tab");
        const fleetPane = byId("fleet-pane");
        if (!visible && fleetTab?.classList?.contains("active"))
            root.bootstrap?.Tab?.getOrCreateInstance(byId("transmitter-hardware-tab"))?.show();
        if (fleetTab) {
            fleetTab.disabled = !visible;
            fleetTab.parentElement.hidden = !visible;
        }
        if (fleetPane) fleetPane.hidden = !visible;
        byId("wtp-controls").hidden = !visible;
        byId("wtp-hidden-selection").hidden = visible || !selected();
        byId("wtp-development").hidden = !visible;
        root.FleetSchedules?.setVisible(visible);
        byId("wtp_use").disabled = !visible;
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => { field.disabled = !visible || !selected(); });
        const transport = read().Transport;
        const network = transport === "network" || transport === "network_plain";
        if (byId("wtp-device-label")) byId("wtp-device-label").textContent =
            transport === "network_plain" ? "Device identity (optional)" : "Device identity";
        if (byId("wtp-device-hint")) byId("wtp-device-hint").textContent =
            transport === "network_plain"
                ? "Leave blank to learn this Pico's identity on first connect; later connections in this session check it."
                : "32 lowercase hexadecimal characters from Pico HELLO.";
        root.document.querySelectorAll("[data-wtp-transport]").forEach(group => {
            group.hidden = group.dataset.wtpTransport !== (network ? "network" : "usb");
        });
        root.document.querySelectorAll("[data-wtp-security]").forEach(group => {
            group.hidden = group.dataset.wtpSecurity !== (transport === "network" ? "tls" : "none");
        });
        const n = snapshot?.network;
        if (byId("wtp-network-state")) byId("wtp-network-state").textContent = n
            ? `Configured target: ${n.hostname}:${n.port} · ${n.state}. ${n.security === "plain_lan" ? "Plain LAN; clients on this network can control the Pico." : `Expected identity: ${n.expected_identity || "unconfirmed"}. Last authenticated identity: ${n.authenticated_identity || "unconfirmed"}.`} Resolved address: ${n.resolved_address || "unresolved"}. Observation age: ${n.observed_ms && snapshot.now_ms ? Math.max(0, Number(BigInt(snapshot.now_ms) - BigInt(n.observed_ms))) + " ms" : "unknown"}. ${n.diagnostic || ""}`
            : "Network connection is unconfirmed.";
        root.WtpManagement?.setAvailability(visible && selected() && transport === "network" && snapshot?.selected === true && snapshot.ready === true && snapshot.phase === "idle" && !snapshot.worker_active && !snapshot.recovery_required && !snapshot.owns);
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
            root.updateCwMessageLengthEstimate?.();
            root.validateCwMessage?.();
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
    let fleetCatalog = null, fleetDiscovery = null, fleetRevision = "", fleetChoice = "", fleetEditId = "", fleetObservedId = "", fleetBusy = false;
    const fleetUrl = suffix => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/host/" + suffix;
    const fleetFeedback = message => { if (byId("fleet-feedback")) byId("fleet-feedback").textContent = message; };
    const editorFeedback = message => { if (byId("fleet-editor-feedback")) byId("fleet-editor-feedback").textContent = message; };
    const fleetCandidate = id => fleetDiscovery?.candidates?.find(candidate => candidate.id === id);
    const fleetProfile = id => fleetCatalog?.profiles?.find(profile => profile.id === id);
    function fleetSelection() {
        if (!fleetChoice) return null;
        const [kind, id] = fleetChoice.split(":", 2);
        return kind === "known" ? {kind, profile: fleetProfile(id)} :
            kind === "nearby" ? {kind, candidate: fleetCandidate(id)} : null;
    }
    function fleetRender() {
        const selector = byId("fleet-device");
        if (!selector || !fleetCatalog) return;
        selector.replaceChildren();
        const group = (label, entries) => {
            const optgroup = root.document.createElement("optgroup"); optgroup.label = label;
            for (const [value, text] of entries) {
                const option = root.document.createElement("option"); option.value = value; option.textContent = text;
                optgroup.append(option);
            }
            selector.append(optgroup);
        };
        group("Saved devices", (fleetCatalog.profiles || []).map(profile => {
            const candidate = fleetCandidate(profile.discovery_id);
            const connected = fleetCatalog.active_id === profile.id && snapshot?.ready &&
                snapshot?.identity?.device_id === profile.settings?.["Device ID"];
            const state = connected ? "connected" : profile.method === "manual" ? "not checked" :
                candidate?.state === "online" ? "advertised; identity unconfirmed" : "offline";
            const endpoint = profile.settings?.Transport === "usb" ? profile.settings?.Endpoint :
                `${profile.settings?.Hostname || "?"}:${profile.settings?.["TCP Port"] || "?"}`;
            return [`known:${profile.id}`, `${profile.name} · ${profile.settings?.Transport || "?"} · ${state} · ${endpoint} · ${(profile.settings?.["Device ID"] || "unverified").slice(0, 8)}…`];
        }));
        group("Nearby advertisements (unverified)", (fleetDiscovery?.candidates || []).map(candidate =>
            [`nearby:${candidate.id}`, `${candidate.instance} · ${candidate.binding || "unknown binding"} · ${candidate.state} · ${candidate.target || "unresolved"}:${candidate.port || "?"} · link ${candidate.interface}`]));
        if (!selector.options.length) group("Devices", [["", "No saved or nearby devices"]]);
        if (!fleetChoice && fleetCatalog.active_id) fleetChoice = `known:${fleetCatalog.active_id}`;
        if ([...selector.options].some(option => option.value === fleetChoice)) selector.value = fleetChoice;
        else { fleetChoice = ""; selector.selectedIndex = -1; }
        selector.disabled = false;
        const selection = fleetSelection();
        const profile = selection?.profile, candidate = selection?.candidate;
        byId("fleet-use").hidden = !profile;
        byId("fleet-edit").hidden = !profile;
        byId("fleet-remove").hidden = !profile;
        byId("fleet-use").disabled = fleetBusy || !profile || profile.legacy_unverified;
        byId("fleet-edit").disabled = fleetBusy || !profile;
        byId("fleet-remove").disabled = fleetBusy || !profile;
        byId("fleet-device-detail").textContent = profile
            ? `${fleetCatalog.active_id === profile.id ? "Active host endpoint. " : "Saved profile. "}${profile.method === "dns_sd" ? "DNS-SD hint; identity must be checked again before use. " : "Manual endpoint. "}${profile.legacy_unverified ? "Confirm the full device ID before use. " : ""}Expected ID: ${profile.settings?.["Device ID"] || "unverified"}. Endpoint: ${profile.settings?.Transport === "usb" ? profile.settings?.Endpoint : `${profile.settings?.Hostname}:${profile.settings?.["TCP Port"]}`}.${candidate?.state === "online" && (candidate.target !== profile.settings?.Hostname || candidate.port !== profile.settings?.["TCP Port"]) ? ` Nearby SRV is now ${candidate.target}:${candidate.port}; edit and verify this profile before use.` : ""} Selecting does not connect or save.`
            : candidate ? `${candidate.binding === "plain" ? "Plain LAN needs your explicit consent and device-ID confirmation. " : "TLS needs device-specific trust files and identity validation. "}SRV: ${candidate.target || "unresolved"}:${candidate.port || "?"}; interface ${candidate.interface}, protocol ${candidate.protocol}, domain ${candidate.domain}. This advertisement is not proof of RF readiness or identity.`
            : fleetCatalog.active_unmatched ? "The active [WTP] endpoint has no matching saved profile. Add it manually if needed." : "Choose a saved device or nearby candidate. Exploring does not change the active endpoint.";
    }
    async function fleetLoad() {
        if (!byId("fleet-device") || !visible || fleetBusy) return;
        try {
            const [catalogResponse, discoveryResponse] = await Promise.all([
                root.fetch(fleetUrl("devices"), {cache:"no-store"}),
                root.fetch(fleetUrl("discovery"), {cache:"no-store"})
            ]);
            if (!catalogResponse.ok) throw new Error(`Catalog unavailable (${catalogResponse.status}).`);
            const catalog = await catalogResponse.json();
            if (catalog.scope !== "wsprrypi-wtp-catalog/1" || !Array.isArray(catalog.profiles))
                throw new Error("Catalog response is invalid.");
            fleetCatalog = catalog;
            fleetRevision = catalogResponse.headers?.get?.("ETag") || "";
            fleetDiscovery = discoveryResponse.ok ? await discoveryResponse.json() :
                {available:false, reason:"Discovery read failed", candidates:[]};
            if (!Array.isArray(fleetDiscovery.candidates)) fleetDiscovery.candidates = [];
            fleetRender();
            byId("fleet-discovery-status").textContent = fleetDiscovery.available ?
                fleetDiscovery.candidates.length ? "Nearby observations are connection hints, not device authentication." :
                    "Avahi is listening, but no service has been observed. Multicast reachability is unverified." :
                `${fleetDiscovery.reason || "Discovery unavailable"}. Manual entry remains available.`;
        } catch (error) { fleetFeedback(error.message); }
    }
    function fleetDraftRead() {
        return {Transport:byId("fleet-binding").value, Hostname:byId("fleet-host").value,
            "TCP Port":Number(byId("fleet-port").value), "TLS Server Identity":byId("fleet-tls-identity").value,
            "TLS CA File":byId("fleet-ca").value, "TLS Client Certificate":byId("fleet-cert").value,
            "TLS Client Key":byId("fleet-key").value, Endpoint:byId("fleet-path").value,
            "USB Serial":byId("fleet-serial").value, "USB Vendor ID":Number(byId("fleet-vendor").value),
            "USB Product ID":Number(byId("fleet-product").value), "Device ID":byId("fleet-identity").value,
            "Start Uncertainty ns":Number(byId("fleet-uncertainty").value),
            "Allow Frequency Adjustment":byId("fleet-adjust").checked};
    }
    function fleetDraftWrite(settings) {
        const s = {...defaults, ...settings};
        for (const [id, key] of [["fleet-binding","Transport"],["fleet-host","Hostname"],["fleet-port","TCP Port"],
            ["fleet-tls-identity","TLS Server Identity"],["fleet-ca","TLS CA File"],["fleet-cert","TLS Client Certificate"],
            ["fleet-key","TLS Client Key"],["fleet-path","Endpoint"],["fleet-serial","USB Serial"],
            ["fleet-vendor","USB Vendor ID"],["fleet-product","USB Product ID"],["fleet-identity","Device ID"],
            ["fleet-uncertainty","Start Uncertainty ns"]]) byId(id).value = String(s[key] ?? "");
        byId("fleet-adjust").checked = s["Allow Frequency Adjustment"] === true;
        byId("fleet-plain-consent").checked = false;
        fleetObservedId = "";
        fleetEditorRender();
    }
    function fleetEditorRender() {
        const binding = byId("fleet-binding")?.value;
        if (!binding) return;
        root.document.querySelectorAll("[data-fleet-network]").forEach(field => field.hidden = binding === "usb");
        root.document.querySelectorAll("[data-fleet-usb]").forEach(field => field.hidden = binding !== "usb");
        root.document.querySelectorAll("[data-fleet-tls]").forEach(field => field.hidden = binding !== "network");
        byId("fleet-plain-consent").closest(".form-check").hidden = binding !== "network_plain";
        const discovered = byId("fleet-method").value === "dns_sd";
        byId("fleet-host").readOnly = false;
        byId("fleet-port").readOnly = false;
        byId("fleet-identify").disabled = !discovered || binding === "usb";
    }
    function fleetEditorOpen(edit) {
        if (!fleetCatalog) { fleetFeedback("Load the device list first."); return; }
        const selection = fleetSelection();
        const profile = edit ? selection?.profile : null;
        if (edit && !profile) return;
        fleetEditId = profile?.id || "";
        byId("fleet-editor").hidden = false;
        byId("fleet-editor-heading").textContent = edit ? "Edit saved device" : "Add device";
        byId("fleet-name").value = profile?.name || selection?.candidate?.instance || "";
        byId("fleet-method").value = profile?.method || (selection?.candidate?.state === "online" ? "dns_sd" : "manual");
        byId("fleet-method").disabled = edit;
        fleetDraftWrite(profile?.settings || (selection?.candidate?.state === "online" ? {
            Transport:selection.candidate.binding === "tls" ? "network" : "network_plain",
            Hostname:selection.candidate.target, "TCP Port":selection.candidate.port
        } : defaults));
        editorFeedback(edit ? "Edit the saved profile. The active endpoint changes only when you use it." :
            "Save a device profile; the active endpoint remains unchanged.");
        byId("fleet-name").focus();
    }
    async function fleetMutation(body, feedback) {
        fleetBusy = true; fleetRender();
        try {
            const response = await root.fetch(fleetUrl("devices"), {method:"POST", cache:"no-store",
                headers:{"Content-Type":"application/json", "X-WsprryPico-Request":"1", "If-Match":fleetRevision},
                body:JSON.stringify(body)});
            const data = await response.json();
            if (!response.ok) throw new Error(data.error?.message || data.error?.code || `Request failed (${response.status}).`);
            fleetRevision = response.headers?.get?.("ETag") || "";
            fleetFeedback(feedback);
            byId("fleet-editor").hidden = true;
            fleetBusy = false;
            await fleetLoad();
        } catch (error) { editorFeedback(`${error.message} Your draft is preserved. Refresh the list to resolve a revision conflict.`); }
        finally { fleetBusy = false; fleetRender(); }
    }
    async function fleetIdentify() {
        const selection = fleetSelection();
        const discoveryId = fleetEditId ? fleetProfile(fleetEditId)?.discovery_id : selection?.candidate?.id;
        if (!discoveryId) { editorFeedback("Choose a nearby candidate first."); return; }
        const settings = fleetDraftRead();
        if (settings.Transport === "network" && (Object.keys(errors(settings, true)).length || !/^[0-9a-f]{32}$/.test(settings["Device ID"]))) {
            editorFeedback("Enter the expected device ID and host TLS trust file references first."); return;
        }
        if (settings.Transport === "network_plain" && !byId("fleet-plain-consent").checked) {
            editorFeedback("Choose Plain LAN explicitly before identifying this candidate."); return;
        }
        editorFeedback("Reading HELLO, STATUS and CAPS from the selected endpoint…");
        try {
            const response = await root.fetch(fleetUrl("discovery/identify"), {method:"POST", cache:"no-store",
                headers:{"Content-Type":"application/json", "X-WsprryPico-Request":"1"},
                body:JSON.stringify({discovery_id:discoveryId, settings})});
            const data = await response.json();
            if (!response.ok) throw new Error(data.error?.message || data.error?.code || "Identification failed.");
            fleetObservedId = data.device_id;
            if (settings.Transport === "network_plain" && root.confirm(`Plain LAN reported WTP device ID ${data.device_id}. This is not cryptographic authentication. Save this as the expected ID?`))
                byId("fleet-identity").value = data.device_id;
            editorFeedback(`Observed WTP device ID ${data.device_id}. ${data.authenticated ? "TLS server identity validated." : "Plain LAN is unauthenticated."} No job was claimed or armed.`);
        } catch (error) { fleetObservedId = ""; editorFeedback(error.message); }
    }
    async function fleetSave() {
        const settings = fleetDraftRead(), name = byId("fleet-name").value.trim();
        const problems = errors(settings, true);
        if (!/^[0-9a-f]{32}$/.test(settings["Device ID"])) problems["Device ID"] = "Enter a confirmed full device ID.";
        if (!name || name.length > 80) problems.Name = "Enter a display name.";
        if (Object.keys(problems).length) { editorFeedback(Object.values(problems)[0]); return; }
        const method = byId("fleet-method").value;
        const selection = fleetSelection();
        const profile = fleetEditId ? fleetProfile(fleetEditId) : null;
        const discoveryId = profile?.discovery_id || selection?.candidate?.id || "";
        if (method === "dns_sd" && !discoveryId) { editorFeedback("Choose a nearby candidate first."); return; }
        if (settings.Transport === "network_plain" && !byId("fleet-plain-consent").checked) {
            editorFeedback("Choose Plain LAN explicitly before saving."); return;
        }
        if (method === "dns_sd" && settings.Transport === "network_plain" && fleetObservedId !== settings["Device ID"] && !profile) {
            editorFeedback("Identify and confirm this Plain LAN device before saving."); return;
        }
        if (!root.confirm(`Save ${name} as a ${settings.Transport} ${method === "dns_sd" ? "nearby" : "manual"} device? The active endpoint will not change.`)) return;
        const unchanged = profile && Object.keys(defaults).every(key => profile.settings?.[key] === settings[key]);
        const body = profile ? unchanged ? {operation:"rename", id:profile.id, name} :
            {operation:"edit", id:profile.id, name, settings, consent_plain:byId("fleet-plain-consent").checked} :
            {operation:"add", name, method, discovery_id:method === "dns_sd" ? discoveryId : "", settings,
             consent_plain:byId("fleet-plain-consent").checked};
        await fleetMutation(body, "Device profile saved. Use this device to apply it to the host.");
    }
    async function fleetUse() {
        const profile = fleetSelection()?.profile;
        if (!profile || fleetBusy) return;
        if (typeof root.hasUnsavedLocalConfigChanges === "function" && root.hasUnsavedLocalConfigChanges()) {
            fleetFeedback("Save or resolve your other setup edits before switching devices."); return;
        }
        if (!root.confirm(`Use ${profile.name} (${profile.settings.Transport}, ${profile.settings["Device ID"]})? This applies its saved endpoint to [WTP]. It does not enable transmission.`)) return;
        fleetBusy = true; fleetRender();
        try {
            if (!hostRevision) {
                const current = await root.fetch(fleetUrl("config"), {cache:"no-store"});
                if (!current.ok) throw new Error("Could not read the current configuration revision.");
                hostRevision = current.headers.get("ETag");
            }
            const response = await root.fetch(fleetUrl("devices/use"), {method:"POST", cache:"no-store",
                headers:{"Content-Type":"application/json", "X-WsprryPico-Request":"1", "If-Match":hostRevision},
                body:JSON.stringify({id:profile.id, catalog_revision:fleetRevision})});
            const data = await response.json();
            if (!response.ok) throw new Error(data.error?.message || data.error?.code || "Selection was rejected.");
            hostRevision = response.headers.get("ETag") || hostRevision;
            fleetFeedback(`${profile.name} was applied to the host. Review Pico status before transmitting.`);
            root.populateConfig?.();
            fleetBusy = false;
            await fleetLoad();
        } catch (error) { fleetFeedback(`${error.message} Review the active endpoint and status before another switch. Your drafts are preserved.`); }
        finally { fleetBusy = false; fleetRender(); }
    }
    async function fleetRemove() {
        const profile = fleetSelection()?.profile;
        if (!profile || !root.confirm(`Remove saved profile ${profile.name}? The active [WTP] endpoint, if this is selected, remains unchanged.`)) return;
        await fleetMutation({operation:"remove", id:profile.id}, "Saved profile removed. The active host endpoint was not changed.");
    }
    function fleetInit() {
        if (!byId("fleet-device")) return;
        byId("fleet-tab")?.addEventListener("shown.bs.tab", fleetLoad);
        byId("fleet-refresh").addEventListener("click", fleetLoad);
        byId("fleet-device").addEventListener("change", event => { fleetChoice = event.target.value; fleetRender(); });
        byId("fleet-add").addEventListener("click", () => fleetEditorOpen(false));
        byId("fleet-edit").addEventListener("click", () => fleetEditorOpen(true));
        byId("fleet-remove").addEventListener("click", fleetRemove);
        byId("fleet-use").addEventListener("click", fleetUse);
        byId("fleet-save").addEventListener("click", fleetSave);
        byId("fleet-identify").addEventListener("click", fleetIdentify);
        byId("fleet-editor-cancel").addEventListener("click", () => { byId("fleet-editor").hidden = true; editorFeedback(""); });
        byId("fleet-binding").addEventListener("change", () => { fleetObservedId = ""; fleetEditorRender(); });
        byId("fleet-method").addEventListener("change", fleetEditorRender);
        byId("fleet-editor").addEventListener("input", () => { fleetObservedId = ""; });
    }
    root.WtpUi = { selected, read, validate, populate,
        get maximumJobDurationNs() {
            const value = snapshot?.capabilities?.max_job_duration_ns;
            const ns = typeof value === "string" && /^\d+$/.test(value) ? BigInt(value) : 0n;
            return (ns > 0n && ns < 3600000000000n ? ns : 3600000000000n).toString();
        },
        get maximumJobDurationSeconds() { return Number(this.maximumJobDurationNs) / 1e9; },
        get maximumJobEvents() {
            const count = snapshot?.capabilities?.max_events;
            return Number.isInteger(count) && count > 0 ? Math.min(512, count) : 512;
        },
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
        fleetInit();
        populate(saved);
        byId("wtp_use").addEventListener("change", () => { render(); root.clickTransmitBackend?.(); root.updateCwMessageLengthEstimate?.(); root.validateCwMessage?.(); request(); });
        const keepFleetTabVisible = () => {
            const tab = byId("fleet-tab");
            if (tab?.classList.contains("active")) tab.scrollIntoView({ block: "nearest", inline: "nearest" });
        };
        byId("fleet-tab")?.addEventListener("shown.bs.tab", keepFleetTabVisible);
        root.addEventListener("resize", keepFleetTabVisible);
        root.document.querySelectorAll("[data-wtp-key]").forEach(field => field.addEventListener("change", () => {
            if (field.dataset.wtpKey === "Transport" && field.value === "network_plain" &&
                Number(byId("wtp_tcp_port")?.value) === 0)
                byId("wtp_tcp_port").value = "31417";
            render(); validate();
        }));
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
