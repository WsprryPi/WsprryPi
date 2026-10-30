// SPDX-License-Identifier: MIT
(function(root) {
    "use strict";
    if (!root.document) return;
    let maintenance = true, observed = false, localPending = false, timer = null, closed = false, polling = false;
    const held = new Map();
    const safeIds = new Set(["wtp-firmware-read", "wtp-firmware-recover", "fleet-refresh", "updateCheckToggle"]);
    function allowed(control) {
        return safeIds.has(control.id) || !!control.closest("nav") ||
            control.matches('[role="tab"], [data-bs-toggle="tab"], .navbar-toggler, .btn-close');
    }
    function locked() { return maintenance || localPending; }
    function render() {
        const active = locked();
        const banner = root.document.getElementById("pico-firmware-safety");
        if (banner) {
            banner.hidden = !active;
            const text = active ? observed || localPending
                ? "Pico firmware maintenance is active. Settings, device selection, transmission, reboot and shutdown are disabled. Use Pico firmware over USB to continue or recover the update."
                : "Checking Pico firmware status before allowing changes. Settings, transmission, reboot and shutdown remain disabled until the controller responds." : "";
            const message = banner.querySelector('.page-warning__message');
            if (message && message.textContent !== text) message.textContent = text;
        }
        if (active) {
            root.document.querySelectorAll("button, input, select, textarea").forEach(control => {
                if (allowed(control)) return;
                if (!held.has(control)) held.set(control, control.disabled);
                if (!control.disabled) control.disabled = true;
            });
        } else if (held.size) {
            for (const [control, disabled] of held) control.disabled = disabled;
            held.clear();
            root.WtpUi?.refreshControls?.();
            root.syncBackendPanelVisibility?.();
            root.syncTransmitAvailabilityUi?.();
        }
    }
    async function poll() {
        if (polling || closed) return;
        polling = true;
        clearTimeout(timer);
        try {
            const response = await root.fetch((root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/status",
                {cache:"no-store", signal:AbortSignal.timeout(5000)});
            if (!response.ok) throw Error("Status unavailable");
            const status = await response.json();
            if (typeof status.host?.firmware_maintenance === "boolean") { maintenance = status.host.firmware_maintenance; observed = true; }
        } catch (_) { /* Retain a known hold when a status read fails. */ }
        polling = false;
        render();
        if (!closed) timer = root.setTimeout(poll, 1500);
    }
    root.PicoFirmwareSafety = {
        get locked() { return locked(); },
        begin() { localPending = true; render(); },
        observeFirmware(state) {
            if (!state) return;
            observed = true;
            localPending = state.maintenance === true || (state.running === true && state.operation !== "plan");
            // A selected member cannot clear another member's global hold.
            // Only the host-wide status poll releases a known maintenance state.
            if (state.maintenance === true) maintenance = true;
            render();
        }
    };
    function init() {
        render();
        // Other UI renderers may re-enable their controls. Reapply the hold
        // without replacing their saved values or triggering change events.
        new root.MutationObserver(() => { if (locked()) render(); }).observe(root.document.body,
            {subtree:true, childList:true, attributes:true, attributeFilter:["disabled"]});
        for (const type of ["click", "input", "change", "submit"]) root.document.addEventListener(type, event => {
            const control = event.target.closest?.("button, input, select, textarea");
            if (locked() && control && !allowed(control)) {
                event.preventDefault(); event.stopImmediatePropagation();
            }
        }, true);
        root.addEventListener("pagehide", () => { closed = true; clearTimeout(timer); });
        root.addEventListener("pageshow", () => { closed = false; void poll(); });
        void poll();
    }
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init);
    else init();
})(typeof window === "undefined" ? globalThis : window);
