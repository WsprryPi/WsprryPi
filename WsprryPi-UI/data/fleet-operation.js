// SPDX-License-Identifier: MIT
(function (root) {
    "use strict";
    const el = id => root.document.getElementById(id), text = (id, value) => {const n = el(id);if (n.textContent !== value) n.textContent = value;};
    let selected = null, frozen = false, latest, confirmation, announced = "";
    const setHidden = (id, hidden) => {el(id).hidden = hidden;};
    function headline(output, row, age, valid) {
        if (!output) return {state: "Removed output", detail: "This selected assignment was removed. Return to This Pi to choose another output."};
        if(output.state==='migration_required')return {state:'Migration required',detail:output.message||'Legacy controller work remains held. Use WTP-capable software to finish cleanup and retirement.'};
        if(output.state==='retired')return {state:'Controller retired',detail:'Retirement is confirmed. Manage the device’s native schedule separately.'};
        const wtp = output.wtp;
        if (output.revoked) return {state: "Removed output", detail: output.message || "Cleanup remains unresolved. Only reconciliation is available."};
        if (output.state === "recovery_required" || wtp?.uncertain || wtp?.safety_fault || wtp?.recovery_required)
            return {state: "Recovery required", detail: output.message || "Resolve controller-owned work before resuming."};
        if (output.state === "unavailable") return {state: "Device unavailable", detail: output.message || "Last observations are historical. Reconcile explicitly to reconnect."};
        if (!valid || age === null || !wtp?.remote) return {state: "Device state unknown", detail: "Read status before resuming. The schedule state does not establish device output."};
        if (age >= 15000n) return {state: "Device status stale", detail: "The last device observation is at least 15 seconds old. Read status or reconcile the connection."};
        if (output.state === "foreign") return {state: "Other job on device", detail: "The assigned schedule does not describe this job. This controller will not stop foreign work."};
        const job = wtp.job, identity = wtp.identity;
        const matched = job?.authoritative && identity && job.device_id === row?.device_id && job.boot_id === identity.boot_id && job.job_id === wtp.job_id;
        if (matched && ["running", "armed", "failed", "missed"].includes(job.state)) return {state: {running: "Running", armed: "Armed", failed: "Failed", missed: "Missed"}[job.state],
            detail: `Matching device job evidence. Output-active observation: ${wtp.remote.output_active ? "active" : "inactive"}; this is not measured RF.`};
        if (output.pending_start) return {state: "Waiting", detail: "The controller accepted a committed pending job; device execution has not been established."};
        return {state: "Idle", detail: "Fresh inactive, unowned device observation. Schedule state is independent."};
    }
    function model(output, row, age, valid) {
        const wtp = output?.wtp, remote = wtp?.remote, identity = wtp?.identity;
        const current = valid && age !== null && age < 15000n && output?.state !== "unavailable" && identity &&
            identity.device_id === row?.device_id && identity.boot_id === output.boot_id && remote?.boot_id === output.boot_id;
        return {...headline(output, row, age, valid), outputObservation: current && typeof remote.output_active === "boolean"
            ? `Device output observation: ${remote.output_active ? "active" : "inactive"} (software report).`
            : "Device output observation: unknown; earlier observations are historical."};
    }
    function render(state = latest) {
        if (!state) return;latest = state;
        const selector = el("operationDevice"), data = state.data, target = root.TargetOperation?.selected;
        if (data || root.TargetOperation) {
            const names = new Map();for (const row of data?.assignments || []) names.set(row.name, (names.get(row.name) || 0) + 1);
            const options = [{id: "", label: root.ControllerMode?.active ? "Controller (no local output)" : "This Pi (controller)"}];
            options.push(...(root.TargetOperation?.options() || []));
            for (const [id, output] of Object.entries(data?.outputs || {})) {
                const row = data.assignments.find(r => r.device_id === id);
                const name = row?.name || output.retained_name || id;
                options.push({id: id + ":" + output.assignment_generation, label: name + (names.get(name) > 1 ? ` · ${row?.product || "output"} ${id.slice(0, 8)}` : "") + " · legacy history" + (output.revoked ? " (removed)" : "")});
            }
            if (selected && !options.some(o => o.id === selected.key)) options.push({id: selected.key, label: selected.name + " (removed)"});
            if (target && !options.some(o => o.id === 'target:'+target.target.device_id)) options.push({id:'target:'+target.target.device_id,label:target.target.name+' (unavailable)'});
            const next = options.map(o => [o.id, o.label]);
            if (JSON.stringify([...selector.options].map(o => [o.value, o.textContent])) !== JSON.stringify(next)) {
                selector.replaceChildren(...options.map(o => {const n = root.document.createElement("option");n.value = o.id;n.textContent = o.label;return n;}));
            }
            selector.value = target ? 'target:'+target.target.device_id : selected?.key || "";
            el("operationDeviceField").hidden = options.length === 1 && !root.ControllerMode?.active;
        }
        selector.disabled = frozen || state.busy || !!root.TargetOperation?.busy;
        const remote = !!selected || !!target;
        const noLocalOutput = root.ControllerMode?.active === true;
        setHidden("operationLocalView", remote || noLocalOutput);el("operationLocalView").inert = remote || noLocalOutput;
        setHidden("operationFleetView", !selected);root.document.body.classList.toggle("fleet-remote-selected", remote || noLocalOutput);
        const controllerView = el("operationControllerView");
        if (controllerView) {
            controllerView.hidden = !noLocalOutput || remote;
            text("operationControllerHeading", root.TargetOperation?.options().length ? "Select a device" : "No managed devices");
            text("operationControllerDetail", "This controller has no local transmitter. Choose a managed device above, or add a device in Setup. Legacy entries show migration history only.");
        }
        for (const label of root.document.querySelectorAll(".times-wrapper .time-label")) {
            const original = label.dataset.localLabel || label.textContent;label.dataset.localLabel = original;
            label.textContent = remote || noLocalOutput ? "Controller " + original : original;
        }
        if (target) return;
        if (!remote) {
            const backend = typeof selectedRuntimeTransmitBackend === "function" ? selectedRuntimeTransmitBackend() : "";
            const hint = el("operationLocalRoute");
            if (hint) {hint.hidden = backend !== "wtp";hint.textContent = "This Pi controls output via its configured WTP endpoint. Endpoint identity and job evidence are reported by that route.";}
            return;
        }
        const output = data?.outputs[selected.id]?.assignment_generation === selected.generation && data?.controller_incarnation === selected.incarnation ? data.outputs[selected.id] : null;
        const row = output ? data?.assignments.find(r => r.device_id === selected.id) : null, age = root.FleetClient.age(output);
        const presentation = model(output, row, age, state.valid);
        text("operationFleetName", row?.name || selected.name);text("operationFleetState", presentation.state);text("operationFleetDetail", presentation.detail);
        text("operationFleetOutput", presentation.outputObservation);
        const historical = !state.valid || age === null || age >= 15000n || output?.state === "unavailable";
        text("operationFleetIdentity", output?.identity ? `${output.identity.product} · ${selected.id} · ${historical ? "last observed firmware" : "firmware"} ${output.identity.firmware_version} · ${({usb: "USB", network_plain: "Plain LAN", network: "TLS network"})[output.transport] || ""} · ${output.trust}${historical ? " (last connection)" : ""}` : selected.id + " · identity not currently verified");
        text("operationFleetAge", age === null ? "Observation age unknown" : `Device observation ${age / 1000n} s old`);
        text("operationFleetScheduleState", row ? row.enabled ? "Saved legacy enable: yes (held)" : "Saved legacy enable: no (held)" : "Assignment removed");
        text("operationFleetMode", row?.schedule?.mode?.toUpperCase() || "Unavailable");
        const mode = row?.schedule?.mode;
        text("operationFleetFrequencyLabel", ({wspr: "Assigned RF center", qrss: "Assigned carrier", tone: "Assigned carrier", fskcw: "Assigned space", dfcw: "Assigned dot"})[mode] || "Assigned frequency");
        text("operationFleetFrequency", row ? `${row.schedule.frequency_hz.toLocaleString()} Hz${row.schedule.shift_hz ? ` · positive shift ${row.schedule.shift_hz} Hz` : ""}` : "Unavailable");
        const pairs = output?.wtp?.adjustments || [];
        const job = output?.wtp?.job, identity = output?.wtp?.identity;
        const accepted = job?.authoritative && identity && job.device_id === row?.device_id && job.boot_id === identity.boot_id && job.job_id === output.wtp.job_id && job.job_id === output.wtp.remote?.job_id;
        text("operationFleetRealized", pairs.length ? `${historical ? "Historical job" : "Job"} LOAD realization: ` + pairs.map(a => `${a.requested_nhz} → ${a.realized_nhz} nHz`).join("; ") + (output.wtp.adjustments_omitted ? "; additional values omitted" : "")
            : accepted ? `${historical ? "Last bound job" : "Current job"} accepted the requested RF targets; no adjustments reported.` : "No current job-bound realization evidence");
        text("operationFleetSchedule", row ? `Every ${row.schedule.period_seconds} s · phase ${row.schedule.phase_seconds} s UTC` : "Unavailable");
        const pending = output?.pending_start, forecast = output?.forecast;
        let start = "No committed pending start";
        if (state.valid && pending && output.utc_anchor_ns && age !== null && age < 15000n) {
            const elapsed = root.FleetClient.elapsedAge(output.utc_anchor_age_ms);
            const delta = elapsed === null ? null : BigInt(pending.start_utc_ns) - BigInt(output.utc_anchor_ns) - elapsed * 1000000n;
            start = delta === null ? "Committed start; clock anchor unavailable" : delta <= 0n ? "Start overdue / awaiting status" : `Committed start in ${(delta + 999999999n) / 1000000000n} s`;

        } else if (forecast) start = `${forecast.if_resumed ? "If resumed now" : "Forecast"}: ${forecast.start_utc_ns} ns UTC`;
        text("operationFleetStart", start);
        const history = output?.wtp?.last_report, recovery = output?.wtp?.last_recovery;
        text("operationFleetHistory", history ? `Historical ${history.outcome} · job ${history.job_id}${history.error ? " · " + history.error : ""}` : recovery ? `Historical reconciliation: ${recovery.cleanup?.ok ? "confirmed" : "unconfirmed"}` : "No historical report");
        const pendingActions = (output?.actions || []).filter(a => a.work_pending);
        const unavailable = frozen || state.busy || !state.valid || !output;
        el("operationFleetEdit").disabled = unavailable || !row;
        setHidden("operationFleetReturn", !!output);setHidden("operationFleetReview", !state.review_required.includes(selected.id) && !state.actions.some(a => a.device_id === selected.id && ["outcome_unconfirmed", "unknown_or_expired", "superseded"].includes(a.state)));
        const action = root.FleetClient.outcome(selected.id, {controller_incarnation: selected.incarnation, assignment_generation: selected.generation}) || pendingActions.at(-1);
        const feedback = state.error || (action ? `${root.FleetClient.actionLabel(action)}. ${action.reason || ""}` : output && !output.can_resume ? output.message || output.resume_reason || "Read device status before resuming." : "");
        if (feedback !== announced) {text("operationFleetFeedback", feedback);announced = feedback;}
    }
    function choose() {
        const value = el("operationDevice").value;
        if (frozen || latest?.busy || root.TargetOperation?.busy) {render();return;}
        if (value.startsWith('target:')) {selected=null;void root.TargetOperation.select(value.slice(7));render();return;}
        root.TargetOperation?.leave();
        if (!value) selected = null;
        else {
            const [id, generation] = value.split(":");
            const output = latest?.data?.outputs[id];if (!output || output.assignment_generation !== generation) return;
            selected = {id, generation, key: value, incarnation: latest.data.controller_incarnation,
                name: latest.data.assignments.find(r => r.device_id === id)?.name || output.retained_name || id};
        }
        announced = "";render();
    }
    function restoreReturnFocus() {
        if (root.sessionStorage.getItem("wsprrypi.fleet.return_focus") !== "operationDevice") return;
        root.sessionStorage.removeItem("wsprrypi.fleet.return_focus");
        const target = el("operationDeviceField").hidden ? el("operationPageTitle") : el("operationDevice");
        if (target.tagName === "H1") target.tabIndex = -1;
        if (!target.disabled) target.focus();
    }
    function init() {
        if (!el("operationDevice")) return;
        root.FleetClient.subscribe(render);restoreReturnFocus();
        root.addEventListener("pageshow", restoreReturnFocus);
        el("operationDevice").addEventListener("change", choose);
        el("operationControllerRefresh")?.addEventListener("click", () => {void root.TargetOperation?.refresh();root.FleetClient.refresh().catch(() => {});});
        el("operationFleetReturn").addEventListener("click", () => {el("operationDevice").value = "";choose();el("operationDevice").focus();});
        el("operationFleetRefresh").addEventListener("click", () => root.FleetClient.refresh().catch(() => {}));
        el("operationFleetReview").addEventListener("click", () => {try {root.FleetClient.review(selected.id);} catch (e) {text("operationFleetFeedback", e.message);}});
        el("operationFleetEdit").addEventListener("click", () => {
            try {const target = {device_id:selected.id};root.sessionStorage.setItem("wsprrypi.fleet.editor.handoff", JSON.stringify({...target, return_focus: "operationFleetEdit"}));
                root.location.href = "index.php?page=config&setup_tab=%23fleet-pane";
            } catch (e) {text("operationFleetFeedback", e.message);}
        });
    }
    root.FleetOperation = {remote: () => !!selected || !!root.TargetOperation?.selected, model, refresh: () => render()};
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", init);else init();
})(window);
