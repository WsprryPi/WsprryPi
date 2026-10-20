// SPDX-License-Identifier: MIT
(function(root) {
    "use strict";
    if (!root.document) return;
    const el = name => root.document.getElementById("wtp-firmware" + (name ? "-" + name : ""));
    let available = false, busy = false, reading = false, state = null, timer = null, boundMember = "", boundSelection = "", generation = 0;
    let closed = false, uncertain = false, requestAction = "", expectedSerial = "", imageEdited = false;
    let clockTimer = null, monitoringSince = null, lastRead = null, failures = 0;
    const url = () => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/member/firmware";
    const erasing = () => state?.operation === "erase" || state?.recovery_operation === "erase" || state?.result?.operation === "erase";
    const erased = () => state?.result?.ok === true && state.result.operation === "erase" &&
        state.result.erase_verified === true && state.result.erased_bytes === 4194304;
    const retryAt = () => state?.result?.error_code === "github_rate_limited" && Number.isSafeInteger(state.result.retry_at)
        && state.result.retry_at > 0 && state.result.retry_at <= 4102444800 ? state.result.retry_at * 1000 : 0;
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
                : requestAction === "erase" ? "Preparing full erase…"
                : action === "plan" ? "Downloading and checking the selected image…"
                : action === "upgrade" ? "Backing up, installing and verifying saved settings…"
                : action === "recover" && erasing() ? "Erasing all flash and verifying every byte…"
                : action === "erase" ? "Preparing full erase…"
                : action === "recover" ? "Recovering and verifying this Pico…" : "Checking firmware status…";
            if (el("phase").textContent !== phase) el("phase").textContent = phase;
            const seconds = Math.floor((Date.now() - monitoringSince) / 1000);
            const age = lastRead === null ? "Waiting for the first status update."
                : `Last status update ${Math.floor((Date.now() - lastRead) / 1000)} s ago.`;
            el("timing").textContent = `Monitoring for ${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}. ${age} Refreshes automatically; a total percentage is not available.`;
        } else {
            monitoringSince = null;
        }
        const retry = retryAt(), remaining = Math.max(0, Math.ceil((retry - Date.now()) / 1000));
        el("retry").hidden = !available || !retry;
        if (retry) el("retry").textContent = remaining
            ? `Download checks paused until ${new Date(retry).toLocaleString(undefined, {timeZoneName:"short"})}. Try again in ${Math.floor(remaining / 60)}:${String(remaining % 60).padStart(2,"0")}. Firmware status keeps refreshing.`
            : "The retry time has passed. Choose Download and check image to try again; downloads do not restart automatically.";
        if ((active || available && remaining > 0) && !closed) {
            if (clockTimer === null) clockTimer = root.setTimeout(() => { clockTimer = null; render(); }, 1000);
        } else {
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
        el("plan").disabled = busy || locked || retryAt() > Date.now() || !available || !state?.selection_id || state?.maintenance === true;
        el("upgrade").disabled = busy || locked || imageEdited || !available || state?.operation !== "plan" || state?.result?.ok !== true || state?.maintenance === true;
        el("erase").disabled = busy || locked || !available || !state?.selection_id || !state?.device_id || state?.maintenance === true;
        el("recover").hidden = state?.maintenance !== true;
        el("recover").disabled = busy || locked || !available || !state?.recovery_plan_id;
        el("recover").textContent = erasing() ? state?.manual_bootsel_required ? "Erase after BOOTSEL" : "Retry full erase"
            : state?.manual_bootsel_required ? "Continue after BOOTSEL" : "Recover this update";
        el("migration").hidden = state?.manual_bootsel_required !== true;
        el("migration-action").textContent = erasing() ? "Erase after BOOTSEL" : "Continue after BOOTSEL";
        el("migration-tail").textContent = erasing() ? "Keep USB connected until erase verification finishes."
            : "Keep USB connected until installation and verification finish.";
        if (state) el("identity").textContent = `USB ${state.serial} · Device ${state.device_id || "unconfirmed"} · Installed ${erased() ? "none (flash erased)" : state.installed_version || "unconfirmed"}`;
        const installerUpdate = state?.result?.installer_update;
        el("installer-update").hidden = installerUpdate?.state !== "retained";
        if (installerUpdate?.state === "retained") el("installer-update").textContent = `Using the retained flash tool. Its update check could not complete: ${installerUpdate.reason || "The compatible update channel is unavailable."}${state.result.ok ? " The checked firmware image remains usable." : ""}`;
        activity();
    }
    function describe() {
        if (erasing()) {
            if (state?.running) return "Keep USB connected. Full erase removes firmware and all saved data; completion requires verification of every flash byte.";
            if (erased())
                return "Erase verified: all 4 MiB of flash is blank. Firmware, credentials and saved data are removed. The Pico remains in BOOTSEL; install firmware before using it again. Existing host backups are retained.";
            if (state?.manual_bootsel_required) return `${state.result?.error ? `Previous attempt: ${state.result.error}. ` : ""}Full erase is prepared. No flash has been erased yet. This Pico remains paused in WsprryPi. Follow the BOOTSEL steps below, then choose Erase after BOOTSEL to erase and verify all flash. No backup will be created.`;
            if (state?.maintenance) return `Erase is unconfirmed. Do not assume credentials or firmware have been removed. ${state.result?.error || "An interrupted erase needs recovery."} Keep the selected Pico in BOOTSEL and choose Retry full erase.`;
            if (state?.result?.error) return `Erase refused: ${state.result.error} Read status before preparing another erase.`;
        }
        if (state?.running) return state.operation === "plan" ? "The Pico has not been changed. The checked image will appear automatically when verification finishes."
            : "Keep the cable connected. Saved settings are checked before this Pico returns to use.";
        const result = state?.result;
        if (imageEdited && state?.operation === "plan" && result?.ok) return "Image selection changed. Download and check a fresh image before installing.";
        if (state?.manual_bootsel_required) return `${result?.ok === false ? `Previous attempt: ${result.error || "Continuation did not complete"}. ` : ""}Ready for a one-time BOOTSEL migration. No firmware has been written. This Pico remains paused in WsprryPi. Follow the reconnect instructions below; saved settings will be backed up and verified during installation.`;
        if (result?.ok && state.operation === "plan") return `Checked ${result.version} · ${result.branch} · source ${result.source} · CI ${result.workflow?.run_id || "unavailable"}/${result.workflow?.attempt || "unavailable"}. Saved settings will be preserved. Review this image before installing.${result.recent_ci?.newer_run ? ` A newer CI run ${result.recent_ci.newer_run.run_id} is ${result.recent_ci.newer_run.status} (${result.recent_ci.newer_run.conclusion || "pending"}); the checked saved image remains selected.` : result.recent_ci?.available === false ? result.recent_ci.error_code === "github_rate_limited" ? ` Additional CI status: ${result.recent_ci.reason} The checked image remains selected.` : " Additional CI run status is unavailable." : ""}`;
        if (result?.ok) return `Verified ${result.version}. Saved settings were preserved and the Pi reconnected to this Pico.`;
        if (state?.maintenance && state?.programming_verified) return `Firmware ${state.recovery_version} is verified. The Pi connection remains unconfirmed. Keep this Pico connected and choose Recover this update to retry the connection without reflashing.`;
        if (state?.maintenance) return `${(result?.error || "An interrupted update needs recovery").replace(/[.]+$/, "")}. This Pico remains paused in WsprryPi. Keep it connected; use Recover this update. An administrator may need the private transaction record and a deliberate BOOTSEL reconnect.`;
        if (result?.error) return `${state.operation === "plan" ? "Image request" : "Firmware update"} refused: ${result.error.replace(/[.]+$/, "")}. ${result.recovery_required === false ? "No firmware was written. " : ""}${retryAt() ? "Wait until the retry time before checking the image again." : "Read status and check the image before retrying."}`;
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
        if (!available || closed || busy || action === "plan" && retryAt() > Date.now() || (action !== "read" && (uncertain || state?.running))) return;
        if (action === "erase" || action === "recover" && erasing()) {
            if (!state?.selection_id || !state?.serial || action === "erase" && (!state.device_id || state.maintenance)) return;
            if (!root.confirm(`${action === "erase" ? "Prepare full erase of" : "Erase ALL firmware and saved data on"} USB Pico ${state.serial}? This deletes Wi-Fi passwords, certificates, Bluetooth bonds and all persistent data from the entire flash. No backup is created. Existing host backups are retained. ${action === "erase" ? "Disable its saved standalone schedule first. You will be asked to reconnect while holding BOOTSEL, then explicitly continue the erase." : "This cannot be undone. Keep USB connected until all flash is verified blank."}`)) return;
        } else if (action === "upgrade" || action === "recover") {
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
            } else if (action === "erase") {
                root.PicoFirmwareSafety?.begin();
                payload = {action,selection_id:state.selection_id,serial:state.serial,confirmed:true};
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
        if (state?.operation === "plan" && !state.running && state.result?.ok) el("feedback").textContent = "Image selection changed. Download and check a fresh image before installing.";
        render();
    }
    function init() {
        if (!el("")) return;
        for (const action of ["read","plan","upgrade","recover","erase"]) el(action).addEventListener("click", () => perform(action));
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
