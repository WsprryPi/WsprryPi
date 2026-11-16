# Phase 5 prompt: legacy-authority interlock and explicit transition

Execute only phase 5 of [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
when separately authorized. Read the [execution rules](execution-rules.md), the full
[plan](../device-owned-solar-scheduling-plan.md), and approved phase 1 handover,
ownership, storage, security, and recovery decisions. Require phases 1–4 gates.

## Deferred Pi security boundary

Apply the approved [sequencing amendment](deferred-pi-security.md). Phase 2
acceptance covers local Pi and existing Pico management. Implement independently
eligible paths only; controller-to-remote-Pi enrollment, editors, management, and
associated migration remain unavailable until the end-of-phase-6 security gate.
Carry their acceptance obligations into that gate; do not substitute unsecured
transport, fabricated capability, or host writes for remote target operations.
Existing security and target-local authority checks remain enforced.

## Objective and inspection

Implement target-local authority fencing before exposing new schedule enablement.
Inspect Fleet assignment/runtime/store/schedule paths, target ownership/admission,
configuration revisions, pending and accepted jobs, recovery journals, startup,
reload, and all management/direct API entry points. Inventory durable assets needed
to reconcile uncertain work; preserve them until safe retirement is verified.

## Required work

1. Implement the approved cross-authority generation/fencing design at the target.
   Enforce it independently of browser state and the current controller process.
   An old controller restarting or reconnecting cannot resume dispatch after its
   authority is retired. Mere catalog deletion, UI hiding, or controller silence
   is insufficient proof of retirement.
2. Retain existing assignments and recovery data. Do not bulk-convert at startup,
   discovery, or selection. Require explicit target- and assignment-bound operator
   intent to review and migrate an assignment. Implement a bounded, observable,
   durable handover with revision/identity checks and idempotent recovery.
3. For migration, inhibit new legacy dispatch, reconcile pending/accepted jobs and
   ownership, positively confirm old-authority retirement, save the target schedule
   disabled, and verify authoritative persistence. Enablement is a separate explicit
   step guarded by this interlock and the complete runtime readiness contract.
   Do not implement the phase 7 enable UI or activate unfinished phase 6 recurrence.
4. An offline previous controller or unknown outstanding work blocks new enablement
   until positively resolved. Establish clean-install eligibility from authoritative
   target ownership records, not an empty browser/controller catalog. Support a
   genuinely clean target without inventing legacy retirement data.
5. On any partial failure leave the new schedule disabled with a recoverable record.
   Handle lost replies and ambiguous writes through readback before retry. Make
   disable/reconciliation and generation fences survive process restart and reboot;
   bound durable writes according to the approved storage policy.
6. Rollback first verifies the target schedule disabled and reconciled, then permits
   the old authority to resume under the approved fencing protocol. Never clear a
   fence simply because a local controller journal was removed or a browser closed.
7. Preserve unmigrated legacy runtime and its current behavior without adding solar
   policy to it. New assignments use target-owned scheduling only, subject to later
   runtime readiness. A Pi schedule whose selected output is WTP remains owned by
   that Pi; do not silently convert its output destination into a native remote job.
8. Apply enforcement to all authoritative write/enable/admission paths, including
   direct API calls bypassing the UI, configuration reloads, and concurrent clients.
   Update coordinated schemas, descriptors, strict validators, paired adapters, and
   documentation where the approved design changes the shared contract.
9. Keep manual/one-shot, test-tone, and externally claimed WTP authorization and
   safety rules distinct from recurrence migration. An interlock cannot grant those
   permissions or silently cancel accepted work. Follow the approved treatment of
   outstanding jobs; unknown completion blocks handover rather than forcing a reset.

## Tests, documentation, and acceptance gate

Use isolated production services, temporary storage, and explicitly simulated
outputs. Inject crashes/restarts and lost replies before and after every durable
handover step. Test pending and accepted work, ownership uncertainty, stale revisions,
replayed requests, offline old controllers, a restarted old controller, concurrent
migration/enable attempts, rollback, target/controller reboot, and a clean install.
Test that unrelated manual/one-shot authorization remains unchanged and that no
migration operation keys outputs or deletes evidence to resolve uncertainty.
Assert no overlapping dispatch authorities, no implicit enablement, and durable,
idempotent recovery. Verify target-local rejection through direct production APIs.

Run focused authority/storage/management regressions and both complete network and
USB suites for shared-interface changes. Document migration, blocked/uncertain
states, clean-install requirements, rollback, and recovery in applicable Fleet,
configuration, and troubleshooting guides, describing only shipped behavior.
Do not perform an actual migration of live user assignments for validation.

The gate requires crash-safe target-enforced fencing and reconciliation before any
new enablement can be exposed. If a predecessor design cannot prove retirement,
report the design gap for approval rather than weaken the interlock. No journal
cleanup or legacy-runtime removal is authorized here. End with the shared report
and review loop; stop before phase 6.
