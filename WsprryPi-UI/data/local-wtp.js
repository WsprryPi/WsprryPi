// SPDX-License-Identifier: MIT
// Local operator workflow. Direct configuration writes retain immediate abort semantics.
(function (root) {
    "use strict";
    const byId = id => root.document.getElementById(id);
    const url = suffix => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/v1") + "/host/" + suffix;
    let pendingChoice = null, busy = false, closed = false, timer;
    function render(state) {
        const panel = byId("local-wtp-control");
        if (!panel) return;
        const message = state.output_unknown ? "Local output is inhibited: remote output-off is unconfirmed. Reconcile before transmitting."
            : state.revocation_unavailable ? "Local output is inhibited: the saved takeover record is unavailable."
            : state.finishing_remote_job ? "Local Enable is saved. The current remote job is finishing; local output will start after output-off is confirmed."
            : state.remote_owner ? `Remote control owns this Pi. Job: ${state.remote_job_id || "none"}. Select Enable to take local control.`
            : state.local_requested && !state.local_effective ? "Local Enable is saved; local output is waiting for safe admission."
            : "";
        panel.hidden = !message && !pendingChoice;
        byId("local-wtp-status").textContent = message;
        byId("local-wtp-recover").hidden = !state.output_unknown;
        byId("local-wtp-recover").disabled = busy;
    }
    async function readStatus() {
        if (closed || busy) return;
        try {
            const response = await root.fetch(url("wtp-endpoint"), {cache: "no-store", signal: AbortSignal.timeout(5000)});
            if (!response.ok) throw new Error("Status unavailable");
            render(await response.json());
        } catch (_) {
            const status = byId("local-wtp-status");
            if (status?.textContent && !status.textContent.startsWith("Last observation"))
                status.textContent = "Last observation (connection unavailable): " + status.textContent;
        }
        finally { if (!closed) timer = setTimeout(readStatus, 3000); }
    }
    function choose(state) {
        return new Promise(resolve => {
            pendingChoice = resolve;
            byId("local-wtp-control").hidden = false;
            byId("local-wtp-confirmation").hidden = false;
            const active = ["armed", "running"].includes(String(state.remote_state).toLowerCase());
            byId("local-wtp-finish").hidden = !active;
            byId("local-wtp-confirm-detail").textContent = active
                ? `A remote job (${state.remote_job_id}) is ${state.remote_state}. End it now or let this job finish before local output starts.`
                : `Remote ownership (${state.remote_owner || "changed"}) is active. Take local control and cancel any loaded job.`;
            byId("local-wtp-decline").focus();
        });
    }
    function settleChoice(choice) {
        const resolve = pendingChoice;
        pendingChoice = null;
        byId("local-wtp-confirmation").hidden = true;
        resolve?.(choice);
    }
    function enable() {
        const result = root.jQuery.Deferred();
        if (busy) return result.reject({status: 409, responseJSON: {message: "A local control action is already pending."}}, "error").promise();
        busy = true;
        clearTimeout(timer);
        (async () => {
            const current = await root.fetch(url("config"), {cache: "no-store", signal: AbortSignal.timeout(5000)});
            if (!current.ok) throw new Error("Could not read the local configuration. Refresh before enabling.");
            let revision = current.headers.get("ETag"), choice = "end_now", confirmed = false, observedOwner = "", observedJob = "";
            for (;;) {
                const response = await root.fetch(url("wtp-endpoint/enable"), {
                    method: "POST", cache: "no-store", signal: AbortSignal.timeout(30000),
                    headers: {"Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": revision},
                    body: JSON.stringify({choice, confirmed, observed_owner: observedOwner, observed_job: observedJob})
                });
                const data = await response.json();
                if (response.status === 409 && data.error?.code === "local_takeover_confirmation_required") {
                    render(data.status);
                    choice = await choose(data.status);
                    if (choice === "decline") { result.reject({status: 0}, "declined"); return; }
                    observedOwner = data.status.remote_owner || "";
                    observedJob = data.status.remote_job_id || "";
                    confirmed = true;
                    continue;
                }
                if (!response.ok) throw new Error(response.status === 412
                    ? "Local settings changed. Review them and select Enable again."
                    : data.error?.message || data.error?.code || "Local Enable was not accepted.");
                revision = response.headers.get("ETag");
                render(data);
                result.resolve(data, "success", {getResponseHeader: name => name === "ETag" ? revision : null});
                return;
            }
        })().catch(error => result.reject({status: 409, responseJSON: {message: error.name === "TimeoutError"
            ? "Local Enable result is unconfirmed. Read status before retrying." : error.message}}, "error"))
            .finally(() => {
                busy = false;
                root.setTimeout(() => {
                    const trigger = byId("transmit");
                    if (trigger && !trigger.disabled && trigger.getClientRects().length) trigger.focus();
                    else {
                        const heading = [...root.document.querySelectorAll("h1,h2")].find(node => node.getClientRects().length);
                        if (heading) {heading.setAttribute("tabindex", "-1");heading.focus();}
                    }
                    readStatus();
                }, 0);
            });
        return result.promise();
    }
    async function recover() {
        if (busy || !root.confirm("Stop remote work and reconcile this Pi's output? This local action does not ask the controller for approval.")) return;
        busy = true;
        byId("local-wtp-status").textContent = "Reconciling local output…";
        try {
            const response = await root.fetch(url("wtp-endpoint/recover"), {method: "POST", cache: "no-store",
                signal: AbortSignal.timeout(30000), headers: {"Content-Type": "application/json", "X-WsprryPico-Request": "1"},
                body: JSON.stringify({confirmed: true})});
            const data = await response.json();
            render(data);
            if (!response.ok) throw new Error(data.error?.message || "Output-off is still unconfirmed.");
        } catch (error) {byId("local-wtp-control").hidden = false; byId("local-wtp-status").textContent = error.message;}
        finally {busy = false; clearTimeout(timer); timer = root.setTimeout(readStatus, 0);}
    }
    function init() {
        if (!byId("local-wtp-control")) return;
        for (const [id, choice] of [["local-wtp-end-now", "end_now"], ["local-wtp-finish", "finish_current"], ["local-wtp-decline", "decline"]])
            byId(id).addEventListener("click", () => settleChoice(choice));
        byId("local-wtp-recover").addEventListener("click", recover);
        readStatus();
        root.addEventListener("pagehide", () => {closed = true; clearTimeout(timer); settleChoice("decline");});
    }
    root.LocalWtp = {enable};
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init); else init();
})(window);
