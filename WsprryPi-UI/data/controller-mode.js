// SPDX-License-Identifier: MIT
(function (root) {
    "use strict";

    const identity = value => typeof value === "string" && /^[0-9a-f]{32}$/.test(value);

    function status(value) {
        if (!value || typeof value !== "object" || Array.isArray(value)) return null;
        if (value.role === "transmitter" && Object.keys(value).length === 1) return {role: value.role};
        if (Object.keys(value).length !== 5 || value.role !== "controller-only" ||
            typeof value.controller_ready !== "boolean" || !identity(value.device_id) ||
            value.local_output !== null || value.local_transmission_available !== false) return null;
        return value;
    }

    if (typeof module !== "undefined" && module.exports) module.exports = {status};
    if (!root.document) return;

    let current = null;
    let phase = "checking";
    let timer, checking, parked;
    const transmitterActions = new Set();
    const node = id => root.document.getElementById(id);
    const active = () => current?.role === "controller-only";
    const available = () => phase === "verified";
    const transmitter = () => current?.role === "transmitter" && available();

    function render() {
        const notice = node("controllerOnlyNotice");
        if (!notice) return;
        root.document.documentElement.dataset.hostRole = active() ? "controller-only" : transmitter() ? "transmitter" : "unknown";
        const retry = node("hostRoleRetry");
        const restoreFocus = retry === root.document.activeElement;
        notice.hidden = transmitter();
        node("hostRoleTitle").textContent = active() ? "Controller host" : "Wsprry Pi connection";
        const text = phase === "changed" ? "The host identity or role changed. Reload this page before managing devices."
            : phase === "unauthorized" ? "This browser is not authorized to verify the host. Check network access, then retry."
            : phase === "unavailable" ? "Host status could not be verified. Retry the connection before using device controls."
            : !current ? "Checking the host before enabling controls."
            : active() && !current.controller_ready ? "The controller is starting. Device management is unavailable."
            : "";
        const details = notice.querySelector(".page-warning__message");
        details.hidden = !text;
        details.textContent = text;
        const host = node("controllerHostIdentity");
        host.hidden = !active();
        host.textContent = active() ? "Controller ID " + current.device_id : "";
        retry.hidden = phase !== "unauthorized" && phase !== "unavailable";
        notice.classList.toggle("alert-warning", !retry.hidden);
        notice.classList.toggle("alert-info", retry.hidden);
        if (restoreFocus && retry.hidden) root.document.getElementById("main-content")?.focus({preventScroll: true});
        for (const panel of root.document.querySelectorAll("[data-verified-host]")) panel.inert = root.ControllerMode.blocked;
        root.SetupTargets?.render?.();
        root.FleetOperation?.refresh?.();
        root.TargetOperation?.render?.();
        root.WtpUi?.refreshControls?.();
        root.FleetSchedules?.setVisible(!active() || root.ControllerMode.ready);
    }

    function publish(nextPhase, next = current) {
        if (phase === "changed") return;
        if (next && current && (current.role !== next.role || current.device_id !== next.device_id)) {
            nextPhase = "changed";
            next = current;
        }
        if (phase === nextPhase && current?.controller_ready === next?.controller_ready && current?.role === next?.role) return;
        phase = nextPhase;
        current = next;
        render();
        root.dispatchEvent(new Event("wsprry-host-status"));
        if (transmitter()) {
            for (const action of transmitterActions) action();
            transmitterActions.clear();
        } else if (active()) {
            transmitterActions.clear();
        }
    }

    function refresh() {
        if (phase === "changed" || parked) return;
        if (checking) return checking;
        clearTimeout(timer);
        checking = (async () => {
            try {
                const response = await root.fetch(root.WSPRRYPI_PATHS?.versionPath || "/version", {
                    credentials: "same-origin", redirect: "error", cache: "no-store", signal: AbortSignal.timeout(5000)
                });
                if (parked) return;
                if (!response.ok) {
                    publish([401, 403].includes(response.status) ? "unauthorized" : "unavailable");
                    return;
                }
                const data = await response.json();
                if (parked) return;
                const next = status(data?.host_status);
                if (next) publish("verified", next);
                else publish("unavailable");
            } catch (_) {
                if (!parked) publish("unavailable");
            } finally {
                checking = null;
                if (!parked && phase !== "changed") timer = root.setTimeout(refresh, 5000);
            }
        })();
        return checking;
    }

    root.ControllerMode = Object.freeze({
        get active() { return active(); },
        get available() { return available(); },
        get transmitter() { return transmitter(); },
        get deviceId() { return active() ? current.device_id : null; },
        get ready() { return active() && available() && current.controller_ready; },
        get blocked() { return !available() || active() && !current.controller_ready; },
        onTransmitter(action) {
            if (transmitter()) action();
            else if (!active()) transmitterActions.add(action);
        },
        render,
        refresh
    });
    root.document.addEventListener("DOMContentLoaded", () => {
        node("hostRoleRetry")?.addEventListener("click", refresh);
        void refresh();
    });
    root.addEventListener("pagehide", () => {parked = true; clearTimeout(timer);});
    root.addEventListener("pageshow", event => {if (event.persisted) {parked = false; void refresh();}});
})(typeof window === "undefined" ? globalThis : window);
