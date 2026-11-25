// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
(function (root) {
    "use strict";
    const defaults = Object.freeze({
        "Transport": "usb", "Hostname": "", "TCP Port": 80, "TLS Server Identity": "",
        "TLS CA File": "", "TLS Client Certificate": "", "TLS Client Key": "",
        "Endpoint": "", "USB Serial": "", "Device ID": "",
        "USB Vendor ID": 0, "USB Product ID": 0,
        "Start Uncertainty ns": 100000000, "Allow Frequency Adjustment": false
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
    function windowsFileReference(value) {
        if (typeof value !== "string" || !/^[a-zA-Z]:[/\\]/.test(value)) return false;
        const parts = value.slice(3).split(/[/\\]/);
        return parts.every(part => {
            if (!part || part === "." || part === ".." || /[. ]$/.test(part) || /[\x00-\x1f\x7f:<>"|?*]/.test(part)) return false;
            const stem = part.split(".")[0].replace(/ +$/, "").replace(/[a-z]/g, letter => String.fromCharCode(letter.charCodeAt(0) - 32));
            return !/^(?:CON|PRN|AUX|NUL|CLOCK\$|CONIN\$|CONOUT\$|(?:COM|LPT)[0-9¹²³])$/.test(stem);
        });
    }
    function absoluteFileReference(value) {
        return typeof value === "string" && (value.startsWith("/") || windowsFileReference(value));
    }
    function usbInterfaceReference(value) {
        if (typeof value !== "string") return false;
        if (value.startsWith("/dev/")) return !value.includes("/../");
        // SetupAPI names are opaque; discovery owns serial, VID/PID, and role.
        return value.startsWith("\\\\?\\") && value.length > 4 && !/[\x00-\x1f\x7f-\uffff\\/:"<>|?*]/.test(value.slice(4));
    }
    function boundedReference(value, maximum) {
        if (typeof value !== "string" || /[\x00-\x1f\x7f]/.test(value)) return false;
        let bytes = 0;
        for (const character of value) {
            const code = character.codePointAt(0);
            if (code >= 0xd800 && code <= 0xdfff) return false;
            bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
            if (bytes > maximum) return false;
        }
        return true;
    }
    function errors(settings, selected) {
        const result = {};
        for (const [key, max] of [["Endpoint", 512], ["USB Serial", 128], ["Device ID", 32]]) {
            const value = settings[key];
            if (!boundedReference(value, max))
                result[key] = `Enter a valid ${key.toLowerCase()}.`;
        }
        if (!["usb", "network_plain", "network"].includes(settings.Transport)) result.Transport = "Choose USB, Consumer Wi-Fi, or HTTPS.";
        const network = settings.Transport === "network" || settings.Transport === "network_plain";
        const tls = settings.Transport === "network";
        for (const key of ["Hostname", ...(tls ? ["TLS Server Identity"] : [])]) {
            if (settings[key] !== "" && !validNetworkIdentity(settings[key]))
                result[key] = "Enter a DNS hostname or literal IP address, without a URL or port.";
        }
        for (const key of ["TLS CA File", "TLS Client Certificate", "TLS Client Key"]) {
            const value = settings[key];
            if (!boundedReference(value, 512)) result[key] = "Enter a valid local file reference.";
            else if (selected && tls && !absoluteFileReference(value)) result[key] = "Enter an absolute file path on the WsprryPi host.";
        }
        if (selected && network && !settings.Hostname) result.Hostname = "Enter the Pico's configured hostname or literal IP address.";
        if (!Number.isInteger(settings["TCP Port"]) || settings["TCP Port"] < (selected && network ? 1 : 0) || settings["TCP Port"] > 65535) result["TCP Port"] = "Enter the configured management port (1–65535).";
        if (selected) {
            if (!network && !usbInterfaceReference(settings.Endpoint)) result.Endpoint = "Select the dedicated management device interface reported by this host.";
            if (!network && !settings["USB Serial"]) result["USB Serial"] = "Enter the selected device's USB serial.";
            if (!(settings.Transport === "usb" && settings["Device ID"] === "") &&
                !/^[0-9a-f]{32}$/.test(settings["Device ID"]))
                result["Device ID"] = "Enter 32 lowercase hexadecimal characters.";
        }
        for (const key of ["USB Vendor ID", "USB Product ID"]) {
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
            : s.usb?.binding_fault === true ? "USB device identity changed. Reconcile the Pico before another job."
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
    let saved={...defaults},snapshot=null,hostRevision="",timer=null,closed=false;
    const selected=()=>false;
    const read=()=>({...saved});
    const validate=()=>true;
    function render(){fleetRender();}
    function populate(value){saved={...defaults,...(value||{})};fleetRender();}
    let fleetCatalog = null, fleetDiscovery = null, fleetRevision = "", fleetChoice = "", fleetEditId = "", fleetObservedId = "", fleetBusy = false;
    let fleetReadFailed = false;
    let fleetLoading = false, fleetUsbId = "", fleetProbeGeneration = 0, fleetOptionsSignature = "";
    const fleetUrl = suffix => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/host/" + suffix;
    const fleetFeedback = message => { if (byId("fleet-feedback")) byId("fleet-feedback").textContent = message; };
    const editorFeedback = message => { if (byId("fleet-editor-feedback")) byId("fleet-editor-feedback").textContent = message; };
    const fleetCandidate = id => [...(fleetDiscovery?.candidates || []),
        ...(fleetDiscovery?.usb?.candidates || [])].find(candidate => candidate.id === id);
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
        const signature = JSON.stringify([fleetCatalog, fleetDiscovery, snapshot?.ready,
            snapshot?.identity?.device_id, snapshot?.common_name, snapshot?.common_name_confirmed]);
        if (signature !== fleetOptionsSignature) {
            fleetOptionsSignature = signature;
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
            group("Attached USB Picos", (fleetDiscovery?.usb?.candidates || []).map(candidate =>
                [`nearby:${candidate.id}`, `${candidate.instance} · USB · attached`]));
            group("Nearby advertisements (unverified)", (fleetDiscovery?.candidates || []).map(candidate =>
                [`nearby:${candidate.id}`, `${candidate.instance} · ${candidate.binding || "unknown binding"} · ${candidate.state} · ${candidate.target || "unresolved"}:${candidate.port || "?"} · link ${candidate.interface}`]));
            if (!selector.options.length) group("Devices", [["", "No saved or nearby devices"]]);
        }
        if (!fleetChoice && fleetCatalog.active_id) fleetChoice = `known:${fleetCatalog.active_id}`;
        if ([...selector.options].some(option => option.value === fleetChoice)) selector.value = fleetChoice;
        else { fleetChoice = ""; selector.selectedIndex = -1; }
        selector.disabled = false;
        const selection = fleetSelection();
        const profile = selection?.profile, candidate = selection?.candidate;
        byId("fleet-edit").hidden = !profile;
        byId("fleet-remove").hidden = !profile;
        byId("fleet-edit").disabled = fleetBusy || !profile;
        byId("fleet-remove").disabled = fleetBusy || !profile;
        if (byId("fleet-manage")) byId("fleet-manage").disabled = fleetBusy || !profile || profile.legacy_unverified;
        root.PicoTargetEditors?.catalogChanged(fleetCatalog.profiles);
        root.SetupTargets?.catalogChanged(fleetCatalog.profiles,[...(fleetDiscovery?.candidates || []),...(fleetDiscovery?.usb?.candidates || [])]);
        if (byId("fleet-uncertainty-default")) byId("fleet-uncertainty-default").disabled = fleetBusy;
        byId("fleet-device-detail").textContent = profile
            ? `${fleetCatalog.active_id === profile.id ? "Saved profile. " : "Saved profile. "}${profile.method === "dns_sd" ? "DNS-SD hint; identity must be checked again before use. " : "Manual endpoint. "}${profile.legacy_unverified ? "Confirm the full device ID before use. " : ""}Expected ID: ${profile.settings?.["Device ID"] || "unverified"}. Endpoint: ${profile.settings?.Transport === "usb" ? profile.settings?.Endpoint : `${profile.settings?.Hostname}:${profile.settings?.["TCP Port"]}`}.${candidate?.state === "online" && (candidate.target !== profile.settings?.Hostname || candidate.port !== profile.settings?.["TCP Port"]) ? ` Nearby SRV is now ${candidate.target}:${candidate.port}; edit and verify this profile before use.` : ""} Selecting does not connect or save.`
            : candidate?.binding === "usb" ? `Attached by USB. Serial: ${candidate.settings["USB Serial"]}. Add device reads this Pico's identity and fills its connection details. Selecting does not connect or save.`
            : candidate ? `${candidate.binding === "consumer" ? "Consumer Wi-Fi requires the expected device ID and an authenticated password login. " : "HTTPS requires device-specific trust and identity validation. "}SRV: ${candidate.target || "unresolved"}:${candidate.port || "?"}; interface ${candidate.interface}, protocol ${candidate.protocol}, domain ${candidate.domain}. This advertisement is not proof of RF readiness or identity.`
            : fleetCatalog.active_unmatched ? "Choose the intended saved management profile." : "Choose a saved device or nearby candidate. Exploring does not connect or save.";
    }
    async function fleetLoad() {
        if (!byId("fleet-device") || root.ControllerMode?.blocked || fleetBusy || fleetLoading) return;
        fleetLoading = true;
        try {
            const [catalogResponse, discoveryResponse] = await Promise.all([
                root.fetch(fleetUrl("devices"), {cache:"no-store"}),
                root.fetch(fleetUrl("discovery"), {cache:"no-store"})
            ]);
            if (!catalogResponse.ok) throw new Error(`Catalog unavailable (${catalogResponse.status}).`);
            const catalog = await readReply(catalogResponse);
            if (catalog.scope !== "wsprrypi-wtp-catalog/1" || !Array.isArray(catalog.profiles))
                throw new Error("Catalog response is invalid.");
            const discovery = discoveryResponse.ok ? await readReply(discoveryResponse) :
                {available:false, reason:"Discovery read failed", candidates:[]};
            if (root.ControllerMode?.blocked) return;
            fleetCatalog = catalog;
            fleetRevision = catalogResponse.headers?.get?.("ETag") || "";
            fleetDiscovery = discovery;
            if (!Array.isArray(fleetDiscovery.candidates)) fleetDiscovery.candidates = [];
            if (!Array.isArray(fleetDiscovery.usb?.candidates)) fleetDiscovery.usb = {available:false, candidates:[]};
            if (!fleetDiscovery.usb.available) fleetDiscovery.usb.candidates = [];
            fleetRender();
            if (fleetReadFailed) fleetFeedback("Device list refreshed.");
            fleetReadFailed = false;
            const network = fleetDiscovery.available ?
                fleetDiscovery.candidates.length ? "Nearby observations are connection hints, not device authentication." :
                    "Avahi is listening, but no service has been observed. Multicast reachability is unverified." :
                `${fleetDiscovery.reason || "Discovery unavailable"}. Manual entry remains available.`;
            const usb = fleetDiscovery.usb;
            byId("fleet-discovery-status").textContent = `${usb.available
                ? usb.reason || (usb.candidates.length ? "Attached USB Picos are listed automatically." : "No USB Pico is attached.")
                : usb.reason || "USB discovery unavailable."} ${network}`;
        } catch (error) {
            fleetReadFailed = true;
            if (fleetDiscovery?.usb) {
                fleetDiscovery.usb = {available:false, reason:"USB discovery unavailable; refresh the device list.", candidates:[]};
                fleetRender();
            }
            fleetFeedback(error.message);
        }
        finally { fleetLoading = false; }
    }
    function fleetDraftRead() {
        return {Transport:byId("fleet-binding").value, Hostname:byId("fleet-host").value,
            "TCP Port":Number(byId("fleet-port").value), "TLS Server Identity":byId("fleet-tls-identity").value,
            "TLS CA File":byId("fleet-ca").value, "TLS Client Certificate":byId("fleet-cert").value,
            "TLS Client Key":byId("fleet-key").value, Endpoint:byId("fleet-path").value,
            "USB Serial":byId("fleet-serial").value, "USB Vendor ID":Number(byId("fleet-vendor").value),
            "USB Product ID":Number(byId("fleet-product").value), "Device ID":byId("fleet-identity").value};
    }
    function fleetDraftWrite(settings) {
        const s = {...defaults, ...settings};
        for (const [id, key] of [["fleet-binding","Transport"],["fleet-host","Hostname"],["fleet-port","TCP Port"],
            ["fleet-tls-identity","TLS Server Identity"],["fleet-ca","TLS CA File"],["fleet-cert","TLS Client Certificate"],
            ["fleet-key","TLS Client Key"],["fleet-path","Endpoint"],["fleet-serial","USB Serial"],
            ["fleet-vendor","USB Vendor ID"],["fleet-product","USB Product ID"],["fleet-identity","Device ID"]]) byId(id).value = String(s[key] ?? "");
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
        byId("fleet-identify").textContent = fleetUsbId ? "Read USB Pico identity" : "Identify selected nearby device";
        byId("fleet-identify").disabled = fleetUsbId ? binding !== "usb" : !discovered || binding !== "network";
    }
    function fleetEditorOpen(edit) {
        if (!fleetCatalog) { fleetFeedback("Load the device list first."); return; }
        const selection = fleetSelection();
        const profile = edit ? selection?.profile : null;
        if (edit && !profile) return;
        fleetEditId = profile?.id || "";
        ++fleetProbeGeneration;
        fleetUsbId = !edit && selection?.candidate?.binding === "usb" ? selection.candidate.id : "";
        byId("fleet-editor").hidden = false;
        byId("fleet-editor-heading").textContent = edit ? "Edit saved device" : "Add device";
        byId("fleet-name").value = profile?.name || selection?.candidate?.instance || "";
        byId("fleet-method").value = profile?.method || (!fleetUsbId && selection?.candidate?.state === "online" ? "dns_sd" : "manual");
        byId("fleet-method").disabled = edit;
        fleetDraftWrite(profile?.settings || (fleetUsbId ? selection.candidate.settings : selection?.candidate?.state === "online" ? {
            Transport:selection.candidate.binding === "tls" ? "network" : "network_plain",
            Hostname:selection.candidate.target, "TCP Port":selection.candidate.port
        } : defaults));
        editorFeedback(edit ? "Edit the saved profile. Changes retire this profile’s previous management connection; verify identity before reconnecting." :
            "Save a device profile; saving does not connect or enable a schedule.");
        byId("fleet-name").focus();
        if (fleetUsbId) void fleetIdentify();
    }
    async function fleetMutation(body, feedback) {
        if (root.ControllerMode?.blocked) {
            fleetFeedback("Controller management is unavailable. Read current controller status before saving.");
            return;
        }
        fleetBusy = true; fleetRender();
        try {
            const response = await root.fetch(fleetUrl("devices"), {method:"POST", cache:"no-store",
                headers:{"Content-Type":"application/json", "X-WsprryPico-Request":"1", "If-Match":fleetRevision},
                body:JSON.stringify(body)});
            const data = await readReply(response);
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
        const discoveryId = fleetUsbId || (fleetEditId ? fleetProfile(fleetEditId)?.discovery_id : selection?.candidate?.id);
        if (!discoveryId) { editorFeedback("Choose a nearby candidate first."); return; }
        const settings = fleetDraftRead();
        const generation = ++fleetProbeGeneration;
        if (settings.Transport === "network" && (Object.keys(errors(settings, true)).length || !/^[0-9a-f]{32}$/.test(settings["Device ID"]))) {
            editorFeedback("Enter the expected device ID and host TLS trust file references first."); return;
        }
        if (settings.Transport === "network_plain") {
            editorFeedback("Enter the expected immutable device ID, save the profile, then authenticate through Consumer Wi-Fi."); return;
        }
        editorFeedback(fleetUsbId ? "Reading the USB Pico's identity…" : "Reading authenticated management identity and native status…");
        try {
            const response = await root.fetch(fleetUrl("discovery/identify"), {method:"POST", cache:"no-store",
                headers:{"Content-Type":"application/json", "X-WsprryPico-Request":"1"},
                body:JSON.stringify({discovery_id:discoveryId, settings})});
            const data = await readReply(response);
            if (generation !== fleetProbeGeneration || byId("fleet-editor").hidden ||
                JSON.stringify(settings) !== JSON.stringify(fleetDraftRead())) return;
            if (!response.ok) throw new Error(data.error?.message || data.error?.code || "Identification failed.");
            if (!/^[0-9a-f]{32}$/.test(data.device_id)) throw new Error("Pico returned an invalid device identity.");
            fleetObservedId = data.device_id;
            if (fleetUsbId) byId("fleet-identity").value = data.device_id;
            editorFeedback(fleetUsbId ? "USB Pico identified. Save this profile, then choose Manage settings. Identification does not enable transmission."
                : `Observed device ID ${data.device_id}. HTTPS server identity validated. No execution ownership was requested.`);
        } catch (error) { if (generation === fleetProbeGeneration) { fleetObservedId = ""; editorFeedback(error.message); } }
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
            editorFeedback("Choose Consumer Wi-Fi explicitly before saving."); return;
        }
        if (!root.confirm(`Save ${name} as a ${settings.Transport} ${method === "dns_sd" ? "nearby" : "manual"} device? Saving does not enable a schedule.`)) return;
        const unchanged = profile && Object.keys(defaults).every(key => profile.settings?.[key] === settings[key]);
        const body = profile ? unchanged ? {operation:"rename", id:profile.id, name} :
            {operation:"edit", id:profile.id, name, settings, consent_plain:byId("fleet-plain-consent").checked} :
            {operation:"add", name, method, discovery_id:method === "dns_sd" ? discoveryId : "", settings,
             consent_plain:byId("fleet-plain-consent").checked};
        await fleetMutation(body, root.ControllerMode?.active
            ? "Device profile saved. Assign it a schedule in Fleet to use it as a remote output."
            : "Device profile saved. Use this device to apply it to the host.");
    }
    async function fleetRemove() {
        const profile = fleetSelection()?.profile;
        if (!profile || !root.confirm(`Remove saved profile ${profile.name}? This does not change the device’s saved settings. Protected recovery history remains subject to its retirement checks.`)) return;
        await fleetMutation({operation:"remove", id:profile.id}, "Saved profile removed. The active host endpoint was not changed.");
    }
    function fleetInit() {
        if (!byId("fleet-device")) return;
        byId("fleet-tab")?.addEventListener("shown.bs.tab", fleetLoad);
        const saveHint = byId("configSaveStatusHint");
        const title = byId("configPageTitle");
        const originalHint = saveHint?.textContent;
        const updateHeader = () => {
            const fleet = byId("fleet-tab")?.classList.contains("active");
            if (title) title.textContent = fleet ? "Fleet" : "Signal setup";
            if (saveHint) saveHint.textContent = fleet
                ? root.ControllerMode?.active ? "Fleet changes use Save buttons. This controller has no local transmitter settings."
                    : "Connection profiles use Save buttons. Each device runs its own native schedule."
                : originalHint;
            root.SetupTargets?.render?.();
        };
        byId("configTabs")?.addEventListener("shown.bs.tab", updateHeader);
        updateHeader();
        byId("fleet-refresh").addEventListener("click", fleetLoad);
        byId("fleet-manage")?.addEventListener("click", () => {
            const profile = fleetSelection()?.profile;
            if (profile && !profile.legacy_unverified) {
                if (!root.SetupTargets) {fleetFeedback("Shared Setup is unavailable. Reload the page before managing settings.");return;}
                byId("setup-device").focus({preventScroll:true});
                void root.SetupTargets.selectProfile(profile);
            }
        });
        byId("fleet-device").addEventListener("change", event => { fleetChoice = event.target.value; fleetRender(); });
        byId("fleet-add").addEventListener("click", () => fleetEditorOpen(false));
        byId("fleet-edit").addEventListener("click", () => fleetEditorOpen(true));
        byId("fleet-remove").addEventListener("click", fleetRemove);
        byId("fleet-save").addEventListener("click", fleetSave);
        byId("fleet-identify").addEventListener("click", fleetIdentify);
        byId("fleet-editor-cancel").addEventListener("click", () => {
            ++fleetProbeGeneration; fleetUsbId = "";
            byId("fleet-editor").hidden = true; editorFeedback("");
            const trigger = byId(fleetEditId ? "fleet-edit" : "fleet-add");
            (trigger && !trigger.hidden && !trigger.disabled ? trigger : byId("fleet-device")).focus();
        });
        byId("fleet-binding").addEventListener("change", () => { fleetObservedId = ""; fleetEditorRender(); });
        byId("fleet-method").addEventListener("change", fleetEditorRender);
        byId("fleet-editor").addEventListener("input", () => { fleetObservedId = ""; });
    }
    root.WtpUi = { selected, read, validate, populate, refreshControls: render,
        get catalogReady() {return !!fleetCatalog;},
        get savedProfiles() {return structuredClone(fleetCatalog?.profiles || []);},
        get nearbyDevices() {return structuredClone([...(fleetDiscovery?.candidates || []),...(fleetDiscovery?.usb?.candidates || [])]);},
        explore(value) {fleetChoice = value;fleetRender();},
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
        select() { render(); },
        unavailable(){return "";}
    };
    function init() {
        if(!byId("wtp-controls"))return;
        fleetInit();
        const refresh=()=>{void fleetLoad().finally(()=>{if(!closed)timer=setTimeout(refresh,5000);});};
        root.addEventListener("wsprry-host-status",()=>void fleetLoad());
        root.addEventListener("pagehide",()=>{closed=true;clearTimeout(timer);});
        root.addEventListener("pageshow",()=>{closed=false;clearTimeout(timer);refresh();});
        render();refresh();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init);
    else init();
})(typeof window === "undefined" ? globalThis : window);
