# Phase 1 prompt: contracts, security, migration, and capabilities

Execute only phase 1 of [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
when separately authorized. Read the [execution rules](execution-rules.md), the full
[plan](../device-owned-solar-scheduling-plan.md), and current repository instructions.
This is a source-grounded design and decision task, not application implementation.

## Objective and starting evidence

Produce a reviewable contract and decision document for shared target management
and device-owned scheduling. Inspect the plan's baseline paths and current owning
sources rather than assuming that its inspected hashes still describe behavior.
Trace local autosave, remote Pi control, Pico management, Fleet assignment/runtime,
ownership, recovery journals, persistence, boot, and startup admission end to end.
Identify every producer, companion consumer, strict validator, schema, adapter,
and actual test entry point. Record supported and missing capabilities separately.

## Required work

1. Inventory current behavior in a matrix by local Pi, remote Pi, Pico WSPR, and
   Pico message recurrence: reads, writes, persistence, apply/restart semantics,
   scheduling, transmit permission, boot, output ownership, time, location, and
   transport authorization. Distinguish verified contracts from assumptions.
2. Define the target-management interface: exact request/response shapes, bounds,
   current version policy, capability discovery, stable identity, profile/session
   generation, boot context, revisions, atomicity, conflicts, idempotency, and
   readback after lost replies. Cover cross-target and late-response refusal.
3. Threat-model remote management independently of ordinary WTP execution.
   Specify provisioning, authentication, authorization, credential storage and
   redaction, TLS/transport choice, browser origin and CSRF defenses, proxy address
   and redirect restrictions, timeouts, resource limits, and revocation. Explicitly
   decide how authorized remote Pis are reached; plain-LAN WTP consent alone is
   insufficient authority to write configuration.
4. Define complete state tables for schedule enablement, solar enablement,
   Transmit, Enable on Boot, ownership/claims, startup readiness, accepted jobs,
   waiting, disable, immediate stop, restart, and recovery. Disabling scheduling
   permits an accepted complete occurrence to finish but admits no new recurrence;
   it neither grants WTP ownership nor bypasses any existing safety guard.
5. Specify recurrence progress storage, atomicity/crash consistency, bounded write
   frequency, restart continuation, duplicate prevention, and clock/configuration
   generation fencing. Define independent override rotation, inherited progression,
   explicit skips, and paired-frame handling without catch-up bursts.
6. Define UTC readiness, supported date range, clock loss/holdover and steps;
   distinguish them from RF calibration and PPS. Use transmitter-site locator
   center as the baseline. Decide whether native Pi GPS stays out of scope or
   needs a separately approved provider contract covering acquisition, source/site
   identity, timestamps, age, restart, and loss. Never silently fall back from GPS.
7. Design the legacy-authority interlock and handover protocol before new schedule
   enablement: target-local fencing, positive old-authority retirement, reconciliation
   of pending/accepted work, clean-install proof, crash recovery, and rollback.
   Offline prior controllers and unknown outstanding work cannot be hand-waved away.
8. Map Pico capabilities honestly. Identify separate WSPR/message enable states;
   do not invent a single atomic master or missing day/night frequency profiles.
   Identify whether any desired interface needs coordinated firmware changes and
   the explicit repository authority required. Inspect current control policy before
   proposing schemas. Do not promise native Pi GPS or unsupported Pico parity.
9. Map each decision and invariant to phase ownership, production source paths,
   negative tests, both required interoperability recipes where applicable, and
   affected operator guides. Explain migration/default behavior for existing valid
   configurations, independently versioned formats, and retained unsupported values.
10. Specify the enable/save transaction: either validate and save the complete
    candidate atomically with enablement, or save disabled, read back, then enable
    that exact revision. Cover unsaved drafts, changed capabilities, partial writes,
    and lost replies; enabling edit fields must never launch last-saved settings.

## Deliverables and acceptance gate

Save the design/decision artifact under `docs/development/issue-473/` with links
from the phase list. Include the capability matrix, security model, exact contracts,
state tables, persistence and handover designs, test map, and unresolved-decision
register. For each material decision, give alternatives, a recommendation, tradeoffs,
and approval status. Source inspection and document/link checks are appropriate;
do not run hardware or implement the proposed APIs while researching them.

Do not treat a recommendation as approved or silently alter the plan's product
contracts. Identify any proposed change to an already specified contract separately
for explicit review. Decision approval does not grant companion edit or Git authority.

Present the concrete decisions for user approval. Keep the phase ⏳ while required
behavior/security decisions remain unapproved, and do not begin phase 2. The gate
requires all such decisions recorded and approved, with no undocumented assumptions
left for later implementation. End with the shared report and review loop.
