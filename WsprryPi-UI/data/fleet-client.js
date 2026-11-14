// SPDX-License-Identifier: MIT
// One host-only read coordinator and bounded tab-local action journal.
(function (root) {
    "use strict";
    const idPattern = /^[0-9a-f]{32}$/, decimal = /^(0|[1-9][0-9]*)$/;
    const isId = value => typeof value === "string" && idPattern.test(value);
    const isDecimal = value => typeof value === "string" && value.length <= 20 && decimal.test(value) && BigInt(value) <= 18446744073709551615n;
    const listeners = new Set(), refs = new Map(), reviews = new Set();
    const storageKey = "wsprrypi.fleet.actions.1";
    let data = null, etag = "", baseline = 0, latency = 0n, valid = false, baselineValid = false, closed = false;
    let timer, reading = false, polling = false, pollAgain = false, fullPending, sequence = 0, cursor = 0, busy = 0;
    const foregroundReads = [], backgroundReads = [];
    let incarnation = "", candidate = "", publication = 0n, lastClock = 0n;
    const base = () => (root.WSPRRYPI_PATHS?.sharedApiPath || "/api/control/v1") + "/host/fleet";
    const mono = () => root.performance.now();
    function persist() {
        try { root.sessionStorage.setItem(storageKey, JSON.stringify([...refs.values()].map(({action_id, device_id, assignment_generation, controller_incarnation, reviewed_boot_id, operation, control_revision, priority, state}) =>
            ({action_id, device_id, assignment_generation, controller_incarnation, reviewed_boot_id, operation, control_revision, priority, state})))); } catch (_) { /* In-memory tracking remains authoritative for this page. */ }
    }
    try {
        const saved = JSON.parse(root.sessionStorage.getItem(storageKey) || "[]");
        if (Array.isArray(saved) && saved.length <= 64) for (const r of saved) {
            if (isId(r.action_id) && isId(r.device_id) && (r.assignment_generation === null || isId(r.assignment_generation)) && isId(r.controller_incarnation) &&
                (r.control_revision === undefined || isDecimal(r.control_revision)) && typeof r.priority === "boolean" && [...refs.values()].filter(x => x.priority === r.priority).length < 32)
                refs.set(r.action_id, {...r, state: "outcome_unconfirmed", work_pending: null});
        }
    } catch (_) { /* Invalid browser storage is not controller evidence. */ }
    function notify() { for (const fn of listeners) fn(view()); }
    function view() { return {data, etag, valid, busy: busy > 0, actions: [...refs.values()], review_required: [...reviews], error: lastError}; }
    let lastError = "";
    function enqueue(fn, foreground = false) {
        return new Promise((resolve, reject) => {(foreground ? foregroundReads : backgroundReads).push({fn, resolve, reject});drain();});
    }
    async function drain() {
        if (reading) return;
        const next = foregroundReads.shift() || backgroundReads.shift();if (!next) return;
        reading = true;
        try {next.resolve(await next.fn());} catch (error) {next.reject(error);}
        finally {reading = false;drain();}
    }
    async function request(suffix, options = {}) {
        const start = mono();
        const response = await root.fetch(base() + suffix, {method: "GET", cache: "no-store", signal: AbortSignal.timeout(options.method === "POST" ? 45000 : 7000), ...options});
        let value;
        try { value = await response.json(); }
        catch (_) { throw new Error(response.ok ? "The controller returned an unreadable reply. Refresh and review before another action." : `The controller is unavailable (HTTP ${response.status}). Read and review status.`); }
        if (!value || typeof value !== "object" || Array.isArray(value))
            throw new Error(response.ok ? "The controller returned an unreadable reply. Refresh and review before another action." : `The controller is unavailable (HTTP ${response.status}). Read and review status.`);
        if (!response.ok)
            throw Object.assign(new Error(response.status === 412 ? "Schedules changed (revision_conflict). Refresh and review before trying again." : value?.error?.message || value?.error?.code || `Controller request failed (${response.status}).`), {status: response.status});
        const received = mono(), elapsed = received - start;
        if (!Number.isFinite(elapsed) || elapsed < 0) throw new Error("Browser clock changed; refresh status.");
        return {value, etag: response.headers.get("ETag"), elapsed, received};
    }
    function validate(value) {
        if (value.scope !== "wsprrypi-wtp-fleet/1" || value.maximum_outputs !== 32 || !Array.isArray(value.assignments) || value.assignments.length > 32 ||
            !value.outputs || typeof value.outputs !== "object" || Array.isArray(value.outputs) || Object.keys(value.outputs).length > 32 ||
            !isId(value.controller_incarnation) || !isDecimal(value.publication_revision) || !isDecimal(value.now_ms)) throw new Error("Invalid Fleet status or capacity; refresh before operating.");
        const seen = new Set();
        for (const row of value.assignments) {
            if (!isId(row.device_id) || seen.has(row.device_id) || typeof row.name !== "string" || !row.schedule || typeof row.schedule !== "object" ||
                typeof row.enabled !== "boolean" || typeof row.in_flight !== "boolean") throw new Error("Invalid Fleet assignment.");
            seen.add(row.device_id);
        }
        for (const [id, output] of Object.entries(value.outputs)) {
            if (!isId(id) || !output || !isId(output.assignment_generation) || !isDecimal(output.control_revision) ||
                !isDecimal(output.observation_revision) || typeof output.can_resume !== "boolean" ||
                !(output.observation_age_ms === null || isDecimal(output.observation_age_ms)) || !Array.isArray(output.actions) || output.actions.length > 2) throw new Error("Invalid Fleet observation binding.");
            for (const action of output.actions) {
                validateAction(action);
                if (action.device_id !== id || action.controller_incarnation !== value.controller_incarnation || action.assignment_generation !== output.assignment_generation)
                    throw new Error("Fleet action belongs to another context. Refresh before operating.");
            }
        }
        return value;
    }
    function validateAction(record) {
        if (!record || typeof record !== "object" || !isId(record.action_id) || !isId(record.device_id) ||
            !isId(record.controller_incarnation) || !isId(record.assignment_generation) || !isDecimal(record.control_revision) ||
            !["accepted", "pending", "confirmed", "refused", "superseded", "outcome_unconfirmed"].includes(record.state) || typeof record.work_pending !== "boolean" ||
            !["assign", "enable", "pause", "schedule", "recover", "remove"].includes(record.operation) ||
            !(record.reviewed_boot_id === null || isId(record.reviewed_boot_id)))
            throw new Error("Invalid Fleet action identity or completion evidence.");
        if ((["accepted", "pending"].includes(record.state) && !record.work_pending) ||
            (["confirmed", "refused"].includes(record.state) && record.work_pending))
            throw new Error("Invalid Fleet action completion evidence.");
    }
    function referencedAction(record) {
        if (record?.state === "unknown_or_expired") {
            if (!isId(record.action_id) || Object.keys(record).length !== 2 ||
                Object.keys(record).some(key => !["action_id", "state"].includes(key)))
                throw new Error("Invalid Fleet action lookup result.");
        } else validateAction(record);
        const r = refs.get(record?.action_id);
        if (!r || (record.controller_incarnation && record.controller_incarnation !== r.controller_incarnation) ||
            (r.assignment_generation !== null && record.assignment_generation && record.assignment_generation !== r.assignment_generation) || (record.device_id && record.device_id !== r.device_id)) return;
        if (record.state !== "unknown_or_expired" && ((r.reviewed_boot_id !== undefined && record.reviewed_boot_id !== r.reviewed_boot_id) ||
            (r.operation !== undefined && record.operation !== r.operation) ||
            (isDecimal(r.control_revision) && record.control_revision !== r.control_revision))) throw new Error("Action result changed its reviewed boot, operation or control revision.");
        return r;
    }
    function mergeAction(record) {
        const r = referencedAction(record);
        if (!r) return;
        if (r.assignment_generation === null && isId(record.assignment_generation)) r.assignment_generation = record.assignment_generation;
        if (["confirmed", "refused"].includes(record.state) && record.work_pending === false) {
            refs.delete(r.action_id);setOutcome(r.device_id, record);
        } else if (["accepted", "pending", "superseded", "outcome_unconfirmed", "unknown_or_expired"].includes(record.state)) {
            Object.assign(r, record);
            if (record.state === "unknown_or_expired") {r.state = "outcome_unconfirmed";r.work_pending = null;}
            setOutcome(r.device_id, {...r});
        }
    }
    function setOutcome(id, record) {
        const old = outcomes.get(id);
        const currentGeneration = data?.outputs[id]?.assignment_generation;
        if (old?.controller_incarnation === incarnation && old.assignment_generation === currentGeneration &&
            (record.controller_incarnation !== incarnation || record.assignment_generation !== currentGeneration)) return;
        if (old && old.action_id !== record.action_id && old.controller_incarnation === record.controller_incarnation && old.assignment_generation === record.assignment_generation &&
            isDecimal(old.control_revision) && (!isDecimal(record.control_revision) || BigInt(record.control_revision) < BigInt(old.control_revision))) return;
        outcomes.set(id, record);
    }
    const outcomes = new Map(); // At most one summary per context; no historical ledger.
    async function full(confirmation = false) {
        if (closed || root.document.hidden) throw new Error("Fleet read suspended; refresh on return.");
        const generation = sequence;
        const result = await request(root.document.getElementById("fleet-schedules") ? "" : "/operations");
        const value = validate(result.value);
        if (generation !== sequence || closed || root.document.hidden) throw new Error("Fleet read retired; refresh status.");
        // Validate every referenced action before installing authority metadata
        // or learning a new assignment generation from this sample.
        for (const output of Object.values(value.outputs)) for (const action of output.actions) referencedAction(action);
        if (value.controller_incarnation !== incarnation) {
            valid = false;baselineValid = false;
            if (candidate !== value.controller_incarnation) {
                candidate = value.controller_incarnation;sequence++;notify();
                if (confirmation) throw new Error("Controller changed again; refresh and review.");
                return full(true); // At most one serialized confirmation per poll.
            }
            incarnation = candidate;candidate = "";publication = 0n;lastClock = 0n;
            for (const r of refs.values()) if (r.controller_incarnation !== incarnation) {r.state = "outcome_unconfirmed";r.work_pending = null;setOutcome(r.device_id, {...r});}
        }
        candidate = "";
        const revision = BigInt(value.publication_revision), clock = BigInt(value.now_ms);
        if (revision < publication || clock < lastClock) {valid = false;baselineValid = false;throw new Error("Fleet publication or clock regressed; refresh and review.");}
        const received = result.received;
        let nextLatency = BigInt(Math.ceil(result.elapsed));
        if (data?.controller_incarnation === incarnation && revision === publication) {
            const elapsed = received - baseline;
            if (!Number.isFinite(elapsed) || elapsed < 0) throw new Error("Browser clock changed; refresh status.");
            // A repeated publication cannot renew any retained observation.
            // Keep one conservative age envelope across cached full replies.
            for (const [id, output] of Object.entries(value.outputs)) {
                const old = data.outputs[id];
                if (!old || old.assignment_generation !== output.assignment_generation || old.observation_revision !== output.observation_revision || old.boot_id !== output.boot_id) continue;
                for (const field of ["observation_age_ms", "host_observation_age_ms", "utc_anchor_age_ms"]) {
                    if (!isDecimal(old[field]) || !isDecimal(output[field])) continue;
                    if (BigInt(output[field]) < BigInt(old[field])) throw new Error("Fleet observation age regressed; refresh and review.");
                    const retained = BigInt(old[field]) + latency + BigInt(Math.ceil(elapsed)) - BigInt(output[field]);
                    if (retained > nextLatency) nextLatency = retained;
                }
            }
        }
        publication = revision;lastClock = clock;data = value;etag = result.etag;baseline = received;latency = nextLatency;
        baselineValid = true;valid = !!etag;lastError = "";
        for (const output of Object.values(data.outputs)) for (const action of output.actions || []) mergeAction(action);
        for (const key of outcomes.keys()) if (!data.outputs[key]) outcomes.delete(key);
        persist();notify();return view();
    }
    function refresh(foreground = false) {
        if (fullPending) return fullPending;
        fullPending = enqueue(full, foreground).catch(error => {valid = false;lastError = error.message;notify();throw error;}).finally(() => {fullPending = null;});
        return fullPending;
    }
    async function lookup() {
        if (closed || root.document.hidden) return;
        if (!incarnation || candidate || !refs.size) return;
        const all = [...refs.values()].filter(r => r.controller_incarnation === incarnation);
        if (!all.length) return;
        const batch = [];for (let n = 0; n < Math.min(32, all.length); n++) batch.push(all[(cursor + n) % all.length]);
        cursor = (cursor + batch.length) % all.length;
        const {value} = await request("/actions?ids=" + batch.map(r => r.action_id).join(","));
        if (value.controller_incarnation !== incarnation || !isDecimal(value.publication_revision) || !value.actions || BigInt(value.publication_revision) < publication) return;
        const requested = new Set(batch.map(r => r.action_id));
        if (Object.keys(value.actions).length !== requested.size || Object.keys(value.actions).some(id => !requested.has(id)))
            throw new Error("Action lookup returned a different reference set.");
        for (const [id, action] of Object.entries(value.actions)) {
            if (action.action_id !== id) throw new Error("Action lookup identity changed.");
            referencedAction(action);
        }
        for (const action of Object.values(value.actions)) mergeAction(action);
        persist();notify();
    }
    async function poll() {
        clearTimeout(timer);
        if (closed || root.document.hidden) return;
        if (polling) {pollAgain = true;return;}
        polling = true;
        try {
            try {await refresh();} catch (_) { /* Reported; keep known-incarnation action lookup alive. */ }
            try {await enqueue(lookup);} catch (_) { /* An action remains unconfirmed. */ }
        } finally {
            polling = false;clearTimeout(timer);
            if (!closed && !root.document.hidden) timer = setTimeout(poll, pollAgain ? 0 : 3000);
            pollAgain = false;
        }
    }
    function elapsedAge(value) {
        if (!valid || !isDecimal(value)) return null;
        const elapsed = mono() - baseline;
        if (elapsed < 0 || !Number.isFinite(elapsed)) return null;
        return BigInt(value) + latency + BigInt(Math.ceil(elapsed));
    }
    function age(output) {
        if (!baselineValid || !output || !isDecimal(output.observation_age_ms)) return null;
        const elapsed = mono() - baseline;
        if (elapsed < 0 || !Number.isFinite(elapsed)) return null;
        return BigInt(output.observation_age_ms) + latency + BigInt(Math.ceil(elapsed));
    }
    function capture(id) {
        const c = data?.outputs[id];
        if (!valid || !c) throw new Error("Read and review the selected output first.");
        return {device_id: id, controller_incarnation: incarnation, assignment_generation: c.assignment_generation,
            reviewed_control_revision: c.control_revision, reviewed_boot_id: c.boot_id, etag};
    }
    async function mutate(command, reviewed) {
        if (!valid || busy) throw new Error("Read and review Fleet status before another action.");
        const priority = command.operation === "pause";
        const targetId = command.device_id || command.settings?.["Device ID"];
        const uncertain = [...refs.values()].some(r => r.device_id === targetId && ["outcome_unconfirmed", "unknown_or_expired"].includes(r.state));
        if ((reviews.has(targetId) || uncertain) && !["pause", "recover"].includes(command.operation))
            throw new Error("Read status and explicitly review the current state before another mutation.");
        const existing = [...refs.values()].filter(r => r.device_id === (command.device_id || command.settings?.["Device ID"]) && r.priority);
        if (priority && existing.length) throw new Error("An earlier stop remains unconfirmed. Read its result and review before another stop.");
        if ([...refs.values()].filter(r => r.priority === priority).length >= 32) throw new Error("Action tracking is full. Read and review earlier actions first.");
        if (!root.crypto?.getRandomValues) throw new Error("Secure action IDs are unavailable in this browser.");
        busy++;notify();
        try {
            const sample = await refresh(true);
            const context = data.outputs[reviewed.device_id];
            if (reviewed.controller_incarnation !== incarnation || (command.operation !== "assign" &&
                (!context || context.assignment_generation !== reviewed.assignment_generation || context.control_revision !== reviewed.reviewed_control_revision)))
                throw new Error("Output changed after review; review it again.");
            const bytes = root.crypto.getRandomValues(new Uint8Array(16));
            const action_id = [...bytes].map(b => b.toString(16).padStart(2, "0")).join("");
            const deadline = BigInt(sample.data.now_ms) + 30000n;
            if (deadline > 18446744073709551615n) throw new Error("Controller clock exceeds its supported range.");
            const body = {...command, action_id, controller_incarnation: reviewed.controller_incarnation,
                assignment_generation: reviewed.assignment_generation, reviewed_control_revision: reviewed.reviewed_control_revision,
                reviewed_boot_id: reviewed.reviewed_boot_id, accept_before_ms: String(deadline)};
            const device_id = command.device_id || command.settings["Device ID"];
            const reference = {action_id, device_id, assignment_generation: reviewed.assignment_generation,
                controller_incarnation: incarnation, reviewed_boot_id: reviewed.reviewed_boot_id, operation: command.operation, priority, state: "outcome_unconfirmed", work_pending: null,
                control_revision: reviewed.reviewed_control_revision === null ? "1" : String(BigInt(reviewed.reviewed_control_revision) + 1n)};
            refs.set(action_id, reference);setOutcome(device_id, reference);persist();
            try {
                const result = await request("", {method: "POST", headers: {"Content-Type": "application/json", "X-WsprryPico-Request": "1", "If-Match": reviewed.etag}, body: JSON.stringify(body)});
                const record = result.value.action;
                validateAction(record);
                if (result.value.controller_incarnation !== reference.controller_incarnation || !isDecimal(result.value.publication_revision) ||
                    record.action_id !== action_id || record.device_id !== device_id || record.controller_incarnation !== reference.controller_incarnation ||
                    (reference.assignment_generation !== null && record.assignment_generation !== reference.assignment_generation) ||
                    record.operation !== reference.operation || record.reviewed_boot_id !== reference.reviewed_boot_id || record.control_revision !== reference.control_revision)
                    throw new Error("Fleet action reply does not match this request. Read and review status.");
                if (command.operation === "assign" && reference.assignment_generation === null) reference.assignment_generation = record.assignment_generation;
                mergeAction(record);persist();notify();return record;
            } catch (error) {
                const known = outcomes.get(device_id);
                if (known?.action_id === action_id && known.controller_incarnation === reference.controller_incarnation &&
                    known.assignment_generation === reference.assignment_generation && !["outcome_unconfirmed", "unknown_or_expired"].includes(known.state)) {
                    // A concurrent protected GET may resolve admission or even
                    // completion before the delayed POST response is lost.
                    if (["accepted", "pending", "confirmed"].includes(known.state)) return known;
                    reviews.add(device_id);throw error;
                }
                reviews.add(device_id);
                const refused = [400, 401, 403, 404, 405, 409, 412, 413, 415, 422, 428, 429].includes(error.status);
                const outcome = {...reference, state: refused ? "refused" : "outcome_unconfirmed", work_pending: refused ? false : null, reason: error.message};
                if (refused) refs.delete(action_id);
                else Object.assign(reference, outcome);
                setOutcome(device_id, outcome);persist();notify();
                throw error;
            }
        } finally {
            // A pre-POST GET cannot authorize review of a newly submitted
            // action. Fence outstanding reads and require a later full sample.
            sequence++;valid = false;busy--;notify();poll();
        }
    }
    function review(id) {
        if (!isId(id)) throw new Error("Select the intended saved output before review.");
        if (!valid || data.replacement_pending) throw new Error("Refresh current controller state before review.");
        if (busy) throw new Error("Wait for the current request before reviewing earlier actions.");
        const output = data.outputs[id];
        // Recovery/in-flight device duty is independent of tab-local results.
        // Only live controller callbacks prevent retiring current references.
        const unresolved = output && ((output.actions || []).some(a => a.work_pending) || output.state === "starting");
        const retired = [...refs].filter(([, r]) => r.device_id === id &&
            (r.controller_incarnation !== incarnation || r.assignment_generation !== output?.assignment_generation || !unresolved));
        if (unresolved && !retired.length) throw new Error("Current work remains unresolved. Reconcile before retiring an earlier action.");
        for (const [key] of retired) refs.delete(key);
        if (!unresolved) reviews.delete(id);
        outcomes.set(id, {device_id: id, controller_incarnation: incarnation, assignment_generation: output?.assignment_generation ?? null,
            state: "earlier_action_unconfirmed", reason: unresolved
            ? "Earlier context references retired; current work still requires reconciliation. The earlier outcome remains unconfirmed."
            : "Current state reviewed; the earlier action's outcome remains unconfirmed."});persist();notify();
    }
    function actionLabel(action) {
        return action.work_pending && ["accepted", "pending"].includes(action.state)
            ? ({enable: "Enabling", pause: "Pausing", recover: "Reconciling", assign: "Saving assignment", schedule: "Saving schedule", remove: "Removing assignment"})[action.operation]
            : action.state.replaceAll("_", " ");
    }
    root.FleetClient = {actionLabel, elapsedAge, subscribe(fn) {listeners.add(fn);fn(view());return () => listeners.delete(fn);}, refresh, mutate, capture, age, review,
        outcome(id, binding) {
            const matches = value => value && (!binding || (value.controller_incarnation === binding.controller_incarnation && value.assignment_generation === binding.assignment_generation));
            const result = outcomes.get(id);
            if (matches(result)) return result;
            return [...refs.values()].filter(r => r.device_id === id && matches(r)).at(-1);
        }, snapshot: view};
    let renderTimer;
    function startRendering() {clearInterval(renderTimer);renderTimer = setInterval(() => {if (!closed && !root.document.hidden) notify();}, 1000);}
    startRendering();
    root.document.addEventListener("visibilitychange", () => {sequence++;valid = false;baselineValid = false;notify();clearTimeout(timer);if (!root.document.hidden) poll();});
    root.addEventListener("pagehide", () => {closed = true;sequence++;valid = false;baselineValid = false;clearTimeout(timer);clearInterval(renderTimer);});
    root.addEventListener("pageshow", event => {if (event.persisted) {closed = false;sequence++;valid = false;baselineValid = false;startRendering();poll();}});
    if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", poll);else poll();
})(window);
