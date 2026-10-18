// SPDX-License-Identifier: MIT
(function(root) {
    "use strict";
    if (!root.document) return;
    const el = name => root.document.getElementById("wtp-firmware" + (name ? "-" + name : ""));
    let available = false, busy = false, reading = false, state = null, timer = null, boundMember = "", boundSelection = "", generation = 0;
    let closed = false, uncertain = false, requestAction = "", expectedSerial = "", imageEdited = false;
    let clockTimer = null, monitoringSince = null, lastRead = null, failures = 0;
    const url = () => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/member/firmware";
    const cancelPoll = () => { root.clearTimeout(timer); timer = null; };
    function schedulePoll(delay = state?.running ? 1500 : 3000) {
        if (!available || closed || busy || timer !== null) return;
        timer = root.setTimeout(() => { timer = null; return perform("read", true); }, delay);
    }
    function activity() {
        const active = available && (state?.running === true || uncertain || requestAction && requestAction !== "read");
        el("activity").hidden = !active;
        if (active) {
            if (monitoringSince === null) monitoringSince = Date.now();
            const action = requestAction && requestAction !== "read" ? requestAction : state?.operation;
            const phase = uncertain ? "Status connection interrupted. Retrying automatically…"
                : requestAction === "plan" ? "Requesting image check…"
                : requestAction === "upgrade" ? "Requesting installation…"
                : requestAction === "recover" ? "Requesting update recovery…"
                : action === "plan" ? "Downloading and checking the selected image…"
                : action === "upgrade" ? "Backing up, installing and verifying saved settings…"
                : action === "recover" ? "Recovering and verifying this Pico…" : "Checking firmware status…";
            if (el("phase").textContent !== phase) el("phase").textContent = phase;
            const seconds = Math.floor((Date.now() - monitoringSince) / 1000);
            const age = lastRead === null ? "Waiting for the first status update."
                : `Last status update ${Math.floor((Date.now() - lastRead) / 1000)} s ago.`;
            el("timing").textContent = `Monitoring for ${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}. ${age} Refreshes automatically; a total percentage is not available.`;
            if (clockTimer === null && !closed) clockTimer = root.setTimeout(() => { clockTimer = null; activity(); }, 1000);
        } else {
            monitoringSince = null;
            root.clearTimeout(clockTimer); clockTimer = null;
        }
    }
    function render() {
        if (!el("")) return;
        el("").hidden = !available;
        const foregroundRequest = busy && (requestAction !== "read" || reading);
        el("").setAttribute("aria-busy", String(foregroundRequest || uncertain || state?.running === true));
        const locked = foregroundRequest || uncertain || state?.running === true;
        for (const name of ["branch", "source", "version"]) el(name).disabled = locked || state?.maintenance === true;
        el("version-field").hidden = el("source").value !== "version";
        el("read").disabled = busy || !available;
        el("read").textContent = reading ? "Reading status…" : "Read firmware status";
        el("plan").disabled = busy || locked || !available || !state?.selection_id || state?.maintenance === true;
        el("upgrade").disabled = busy || locked || imageEdited || !available || state?.operation !== "plan" || state?.result?.ok !== true || state?.maintenance === true;
        el("recover").hidden = state?.maintenance !== true;
        el("recover").disabled = busy || locked || !available || !state?.recovery_plan_id;
        el("recover").textContent = state?.manual_bootsel_required ? "Continue after BOOTSEL" : "Recover this update";
        el("migration").hidden = state?.manual_bootsel_required !== true;
        if (state) el("identity").textContent = `USB ${state.serial} · Device ${state.device_id || "unconfirmed"} · Installed ${state.installed_version || "unconfirmed"}`;
        activity();
    }
    function describe() {
        if (state?.running) return state.operation === "plan" ? "The Pico has not been changed. The checked image will appear automatically when verification finishes."
            : "Keep the cable connected. Saved settings are checked before this Pico returns to use.";
        const result = state?.result;
        if (imageEdited && state?.operation === "plan") return "Image selection changed. Download and check a fresh image before installing.";
        if (state?.manual_bootsel_required) return `${result?.ok === false ? `Previous attempt: ${result.error || "Continuation did not complete"}. ` : ""}Ready for a one-time BOOTSEL migration. No firmware has been written. This Pico remains paused in WsprryPi. Follow the reconnect instructions below; saved settings will be backed up and verified during installation.`;
        if (result?.ok && state.operation === "plan") return `Checked ${result.version} · ${result.branch} · source ${result.source} · CI ${result.workflow?.run_id || "unavailable"}/${result.workflow?.attempt || "unavailable"}. Saved settings will be preserved. Review this image before installing.${result.recent_ci?.newer_run ? ` A newer CI run ${result.recent_ci.newer_run.run_id} is ${result.recent_ci.newer_run.status} (${result.recent_ci.newer_run.conclusion || "pending"}); the checked saved image remains selected.` : result.recent_ci?.available === false ? " Additional CI run status is unavailable." : ""}`;
        if (result?.ok) return `Verified ${result.version}. Saved settings were preserved and the Pi reconnected to this Pico.`;
        if (state?.maintenance && state?.programming_verified) return `Firmware ${state.recovery_version} is verified. The Pi connection remains unconfirmed. Keep this Pico connected and choose Recover this update to retry the connection without reflashing.`;
        if (state?.maintenance) return `${(result?.error || "An interrupted update needs recovery").replace(/[.]+$/, "")}. This Pico remains paused in WsprryPi. Keep it connected; use Recover this update. An administrator may need the private transaction record and a deliberate BOOTSEL reconnect.`;
        if (result?.error) return `${state.operation === "plan" ? "Image request" : "Firmware update"} refused: ${result.error.replace(/[.]+$/, "")}. ${result.recovery_required === false ? "No firmware was written. " : ""}Read status and check the image before retrying.`;
        return "Select a branch and image, then download and check it. An unavailable branch or stable release is refused without substitution. Developer builds use the host command line.";
    }
    async function exchange(payload) {
        const response = await root.fetch(url(), {method:payload ? "POST" : "GET", cache:"no-store", signal:AbortSignal.timeout(12000),
            ...(payload ? {headers:{"Content-Type":"application/json","X-WsprryPico-Request":"1"},body:JSON.stringify(payload)} : {})});
        const value = await response.json();
        if (!response.ok) throw new Error(value?.error?.message || "Firmware status is unconfirmed. Read status before retrying.");
        return value;
    }
    async function perform(action, automatic = false) {
        if (!available || closed || busy || (action !== "read" && (uncertain || state?.running))) return;
        if (action === "upgrade" || action === "recover") {
            const selected = action === "upgrade" ? state.result?.version : state.recovery_version;
            const verb = action === "upgrade" ? "Install" : state.manual_bootsel_required ? "Continue the BOOTSEL migration to" : "Recover";
            if (!root.confirm(`${verb} ${selected} on USB Pico ${state.serial}? Saved settings are retained. ${action === "upgrade" ? "Older firmware may ask you to reconnect while holding BOOTSEL. " : ""}Keep USB connected during programming and verification.`)) return;
        }
        const requestGeneration = generation;
        busy = true; requestAction = action; reading = action === "read" && !automatic; cancelPoll(); render();
        if (reading) el("read-status").textContent = "Reading firmware status from this Pi…";
        if (action !== "read") el("feedback").textContent = action === "plan"
            ? "Waiting for the Pi to accept the image check. The Pico has not been changed."
            : "Waiting for the Pi to accept the request. Keep USB connected.";
        try {
            let payload;
            if (action === "plan") {
                payload = {action,selection_id:state.selection_id,branch:el("branch").value};
                if (el("source").value === "version") payload.version = el("version").value.trim();
                if (el("source").value === "stable") payload.stable = true;
            } else if (action === "upgrade" || action === "recover") {
                root.PicoFirmwareSafety?.begin();
                payload = {action,selection_id:state.selection_id,plan_id:action === "upgrade" ? state.plan_id : state.recovery_plan_id,confirmed:true};
            }
            const observed = await exchange(payload);
            if (requestGeneration !== generation) return;
            if ((expectedSerial && observed.serial !== expectedSerial) || (boundSelection && observed.selection_id !== boundSelection))
                throw new Error("The selected USB Pico context changed. Waiting for a fresh status binding.");
            state = observed;
            uncertain = false; failures = 0; lastRead = Date.now();
            if (action === "plan") imageEdited = false;
            root.PicoFirmwareSafety?.observeFirmware(state);
            el("feedback").textContent = describe();
            el("read-status").setAttribute("aria-live", automatic ? "off" : "polite");
            el("read-status").textContent = `Status ${automatic ? "updated automatically" : "read"} at ${new Date().toLocaleTimeString()}.`;
        } catch(error) {
            if (requestGeneration !== generation) return;
            // Never replay a mutation after an ambiguous browser/network failure.
            uncertain = true; ++failures;
            if (reading) el("read-status").textContent = "Status read failed. The update state is unconfirmed.";
            el("feedback").textContent = `${error.message} A requested update may already be running. Retrying status automatically; an installation is never repeated automatically.`;
        } finally {
            busy = false; reading = false; requestAction = ""; render();
            schedulePoll(failures ? Math.min(10000, failures * 3000) : state?.running ? 1500 : 3000);
        }
    }
    function invalidate() {
        imageEdited = true;
        if (state?.operation === "plan" && !state.running) el("feedback").textContent = "Image selection changed. Download and check a fresh image before installing.";
        render();
    }
    function init() {
        if (!el("")) return;
        for (const action of ["read","plan","upgrade","recover"]) el(action).addEventListener("click", () => perform(action));
        for (const name of ["branch","source","version"]) el(name).addEventListener("change", invalidate);
        root.WtpFirmware = {setAvailability(value, member = "", selection = "", serial = "") {
            const changed = (boundMember && member && boundMember !== member) || (boundSelection && selection && boundSelection !== selection);
            if ((available && !value) || changed) {
                ++generation; state = null; cancelPoll(); uncertain = false; imageEdited = false; lastRead = null; monitoringSince = null; failures = 0;
                el("identity").textContent = "Read the selected Pico first.";
                el("read-status").textContent = "";
                el("feedback").textContent = "Selected Pico changed. Read its firmware status and check a fresh image before installing.";
            }
            if (member) boundMember = member;
            if (selection) boundSelection = selection;
            expectedSerial = serial;
            available = value; render();
            schedulePoll(state ? undefined : 0);
        }};
        root.addEventListener?.("pagehide", () => { closed = true; cancelPoll(); root.clearTimeout(clockTimer); clockTimer = null; });
        root.addEventListener?.("pageshow", () => { closed = false; activity(); schedulePoll(0); });
        root.document.addEventListener?.("visibilitychange", () => {
            if (root.document.visibilityState !== "hidden") { cancelPoll(); schedulePoll(0); }
        });
        render();
    }
    if(root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded",init); else init();
})(typeof window === "undefined" ? globalThis : window);
