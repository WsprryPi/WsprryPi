// SPDX-License-Identifier: MIT
(function(root) {
    "use strict";
    if (!root.document) return;
    const el = name => root.document.getElementById("wtp-firmware" + (name ? "-" + name : ""));
    let available = false, busy = false, state = null, timer = null, boundMember = "", boundSelection = "", generation = 0;
    const url = () => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/member/firmware";
    function render() {
        if (!el("")) return;
        el("").hidden = !available;
        el("").setAttribute("aria-busy", String(busy || state?.running === true));
        const locked = busy || state?.running === true;
        for (const name of ["branch", "source", "version"]) el(name).disabled = locked || state?.maintenance === true;
        el("version-field").hidden = el("source").value !== "version";
        el("read").disabled = busy || !available;
        el("plan").disabled = locked || !available || !state?.selection_id || state?.maintenance === true;
        el("upgrade").disabled = locked || !available || state?.operation !== "plan" || state?.result?.ok !== true || state?.maintenance === true;
        el("recover").hidden = state?.maintenance !== true;
        el("recover").disabled = locked || !available || !state?.recovery_plan_id;
        if (state) el("identity").textContent = `USB ${state.serial} · Device ${state.device_id || "unconfirmed"} · Installed ${state.installed_version || "unconfirmed"}`;
    }
    function describe() {
        if (state?.running) return state.operation === "plan" ? "Downloading and checking the selected image. The Pico has not been changed."
            : "Firmware work is running. Download, backup, programming and USB verification must finish before this Pico returns to use. Keep the cable connected.";
        const result = state?.result;
        if (result?.ok && state.operation === "plan") return `Checked ${result.version} · ${result.branch} · source ${result.source} · CI ${result.workflow?.run_id || "unavailable"}/${result.workflow?.attempt || "unavailable"}. Saved settings will be preserved. Review this image before installing.${result.recent_ci?.newer_run ? ` A newer CI run ${result.recent_ci.newer_run.run_id} is ${result.recent_ci.newer_run.status} (${result.recent_ci.newer_run.conclusion || "pending"}); the checked saved image remains selected.` : result.recent_ci?.available === false ? " Additional CI run status is unavailable." : ""}`;
        if (result?.ok) return `Verified ${result.version}. Saved settings were preserved and the Pi reconnected to this Pico.`;
        if (state?.maintenance) return `${result?.error || "An interrupted update needs recovery"}. This Pico remains paused in WsprryPi. Keep it connected; use Recover this update. An administrator may need the private transaction record and a deliberate BOOTSEL reconnect.`;
        if (result?.error) return `Image request refused: ${result.error}. Read status and select an available image before retrying.`;
        return "Select a branch and image, then download and check it. An unavailable branch or stable release is refused without substitution. Developer builds use the host command line.";
    }
    async function exchange(payload) {
        const response = await root.fetch(url(), {method:payload ? "POST" : "GET", cache:"no-store", signal:AbortSignal.timeout(12000),
            ...(payload ? {headers:{"Content-Type":"application/json","X-WsprryPico-Request":"1"},body:JSON.stringify(payload)} : {})});
        const value = await response.json();
        if (!response.ok) throw new Error(value?.error?.message || "Firmware status is unconfirmed. Read status before retrying.");
        return value;
    }
    async function perform(action) {
        if (!available || busy) return;
        const requestGeneration = generation;
        busy = true; clearTimeout(timer); render();
        try {
            let payload;
            if (action === "plan") {
                payload = {action,selection_id:state.selection_id,branch:el("branch").value};
                if (el("source").value === "version") payload.version = el("version").value.trim();
                if (el("source").value === "stable") payload.stable = true;
            } else if (action === "upgrade" || action === "recover") {
                const selected = action === "upgrade" ? state.result?.version : state.recovery_version;
                if (!root.confirm(`${action === "upgrade" ? "Install" : "Recover"} ${selected} on USB Pico ${state.serial}? Saved settings are retained. Keep USB connected until verification finishes.`)) return;
                payload = {action,selection_id:state.selection_id,plan_id:action === "upgrade" ? state.plan_id : state.recovery_plan_id,confirmed:true};
            }
            const observed = await exchange(payload);
            if (requestGeneration !== generation) return;
            state = observed;
            el("feedback").textContent = describe();
        } catch(error) {
            if (requestGeneration !== generation) return;
            // Never replay a mutation after an ambiguous browser/network failure.
            state = null;
            el("feedback").textContent = `${error.message} A requested update may already be running. Read firmware status before another request.`;
        } finally {
            busy = false; render();
            if (available && state?.running) timer = root.setTimeout(() => perform("read"), 1500);
        }
    }
    function invalidate() {
        if (state?.operation === "plan" && !state.running) { state.result = null; el("feedback").textContent = "Image selection changed. Download and check a fresh image before installing."; }
        render();
    }
    function init() {
        if (!el("")) return;
        for (const action of ["read","plan","upgrade","recover"]) el(action).addEventListener("click", () => perform(action));
        for (const name of ["branch","source","version"]) el(name).addEventListener("change", invalidate);
        root.WtpFirmware = {setAvailability(value, member = "", selection = "") {
            const changed = (boundMember && member && boundMember !== member) || (boundSelection && selection && boundSelection !== selection);
            if ((available && !value) || changed) {
                ++generation; state = null; clearTimeout(timer);
                el("identity").textContent = "Read the selected Pico first.";
                el("feedback").textContent = "Selected Pico changed. Read its firmware status and check a fresh image before installing.";
            }
            if (member) boundMember = member;
            if (selection) boundSelection = selection;
            available = value; render();
        }};
        render();
    }
    if(root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded",init); else init();
})(typeof window === "undefined" ? globalThis : window);
