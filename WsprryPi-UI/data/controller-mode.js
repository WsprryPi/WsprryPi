// SPDX-License-Identifier: MIT
(function (root) {
    "use strict";
    function status(value) {
        if (!value || typeof value !== "object" || Array.isArray(value) ||
            value.role !== "controller-only" || typeof value.controller_ready !== "boolean" ||
            typeof value.device_id !== "string" || !/^[0-9a-f]{32}$/.test(value.device_id) || value.local_output !== null ||
            value.local_transmission_available !== false) return null;
        return Object.freeze({role:value.role, controller_ready:value.controller_ready,
            device_id:value.device_id, local_output:null, local_transmission_available:false});
    }
    if (typeof module !== "undefined" && module.exports) module.exports = {status};
    if (!root.document) return;
    let current = null, changed = false, initialized = false;
    const node = id => root.document.getElementById(id);
    function render() {
        if (!current) return;
        const ready = current.controller_ready && !changed;
        const notice = node("controllerOnlyNotice");
        if (notice) {
            notice.hidden = false;
            const detail = notice.querySelector(".page-warning__message");
            detail.textContent = changed ? "The controller identity changed. Reload this page before managing devices."
                : ready ? "This controller has no local transmitter. Manage saved devices and assignments in Fleet, or select an assigned output in Operations."
                : "The controller is starting. Device management remains unavailable until it is ready.";
        }
        for (const id of ["radio-pane", "transmitter-hardware-pane", "pi-hardware-pane", "local-wtp-control"]) {
            const panel = node(id);
            if (panel) {
                panel.inert = true;
                panel.querySelectorAll("input,select,textarea,button").forEach(control => {control.disabled = true;});
            }
        }
        if (node("operationLocalView")) {node("operationLocalView").hidden = true;node("operationLocalView").inert = true;}
        for (const id of ["radio-tab", "transmitter-hardware-tab", "pi-hardware-tab"]) {
            const tab = node(id);
            if (tab && !root.SetupTargets?.managingPico) {tab.disabled = true;tab.setAttribute("aria-disabled", "true");}
        }
        for (const id of ["transmit", "stop_transmit", "rebootButton", "shutdownButton"]) {
            const control = node(id);
            if (control) control.disabled = true;
        }
        root.suspendConfigAutosave?.(true);
        root.syncMachinePowerControls?.();
        root.WtpUi?.refreshControls?.();
        root.FleetSchedules?.setVisible(ready && root.PicoFirmwareSafety?.locked !== true);
        const catalog = root.document.querySelector(".fleet-catalog");
        if (catalog) catalog.inert = !ready || root.PicoFirmwareSafety?.locked === true;
        if (root.FleetOperation?.refresh) root.FleetOperation.refresh();
        const hint = node("configSaveStatusHint");
        if (hint) hint.textContent = "Fleet changes use Save buttons. This controller has no local transmitter settings.";
        const saved = node("configSaveStatus");
        if (saved) {saved.textContent = "";saved.removeAttribute("data-state");}
        if (node("configSaveStatusDetail")) node("configSaveStatusDetail").hidden = true;
        root.SetupTargets?.render?.();
        if (!initialized && node("fleet-tab")) {
            initialized = true;
            root.bootstrap.Tab.getOrCreateInstance(node("fleet-tab")).show();
        }
    }
    root.ControllerMode = {
        get active() {return current !== null;},
        get ready() {return !!current?.controller_ready && !changed && root.PicoFirmwareSafety?.locked !== true;},
        accept(value) {
            const next = status(value);
            if (!next) return false;
            if (current && current.device_id !== next.device_id) changed = true;
            current = next;
            render();
            return true;
        },
        render
    };
})(typeof window === "undefined" ? globalThis : window);
