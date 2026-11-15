# Phase 8 prompt: documentation, integration, and closeout

Execute only phase 8 of [issue #473](https://github.com/WsprryPi/WsprryPi/issues/473)
when separately authorized. Read the [execution rules](execution-rules.md), the full
[plan](../device-owned-solar-scheduling-plan.md), approved phase 1 decisions, and
acceptance evidence for phases 1–7. Reinspect actual sources, repository authority,
branches, dirty changes, and companion availability before relying on prior results.

## Objective and scope

Finish cross-repository documentation and validate the integrated candidates against
the entire issue 473 contract. Documentation and required integration checks were
concomitant obligations in prior phases; this phase verifies completeness rather
than excusing earlier gaps. No release, deployment, hardware qualification, issue
closure, PR creation, commit, or push is authorized by invoking this prompt alone.

## Required work

1. Build a requirement-to-evidence matrix covering the plan's product contracts,
   approved decisions, every phase gate, capability limitations, state tables,
   threat model, strict schemas, migration/recovery, ownership, configuration,
   recurrence, UI, and two-target autonomy. Identify exact candidate revisions and
   tested uncommitted changes. Label implementation, proposals, and unresolved
   items honestly; evidence from a different snapshot does not qualify a candidate.
2. Verify all intended companion changes are available on the intended paired refs.
   Distinguish local tests from delivery to devel/main. Recheck current control
   policy and version boundaries, producer/consumer consistency, strict refusal
   before writes, and protocol/security/provider engineering docs in owning repos.
   Lack of companion edit authority or missing candidate dependencies is a blocker,
   not permission to weaken checks or use an older peer.
3. Reconcile operator documentation with actual behavior in the real sibling Docs
   repository. Inspect its instructions and existing changes first. Review/update:
   - `docs/User_Interface/Setup/index.md`, Signal_Setup WSPR/CW guides,
     Scheduling, Transmitter, Pi_IO/Device I/O names, links, and screenshots;
   - `docs/User_Interface/Operations/index.md`, `first_transmission.md`, and
     Fleet/device-selection, controller-only, and remote-management guidance;
   - `docs/Advanced_Operations/ini_configuration/`, defaults, boot/Transmit,
     inheritance, enable/save/apply, site location/time holds, legacy migration,
     recovery, and configuration troubleshooting.
   Verify actual paths instead of creating duplicate material from historical names.
   Explain capabilities, operation, meaningful limitations, and recovery directly;
   omit development diaries, test narration, and promises of unsupported features.
4. Activate Docs `docs/.venv`, run
   `python -m sphinx -b html docs docs/_build/html`, inspect warnings and broken
   links, and render affected desktop/mobile pages. Use Impeccable for rendered
   UI documentation and application reviews. Distinguish pre-existing warnings from
   new ones; resolve introduced warnings and explain remaining limitations rather
   than treating a successful exit alone as a clean documentation build.
   Replace screenshots only when materially inaccurate and verify they match the intended candidate and theme.
5. Inspect checked-in recipes and run the full required unprivileged software
   validation matrix for the candidate pair. Include deterministic solar geometry,
   complete config round trips/failed writes, management/security negatives,
   authority crash recovery, application admission/progress/lifecycle, two-target
   isolation/autonomy, UI/source regressions, and the appropriate semantics profile.
   Use the plan's named focused targets where applicable. Run both complete Debian
   network and USB recipes with actual production paths and pinned dependencies;
   preserve crypto, serialization, framing/ACK, and journal coverage.
6. Review shared Setup and Scheduling through production interfaces at desktop/
   mobile sizes, light/dark, keyboard/accessible states, loading/failure, unsupported
   capability, disconnect/reconnect, stale replies, per-target drafts, uncertain
   writes, and enable/save sequences. Capture evidence and fix applicable findings.
   Verify no controller recurrence dispatch, sunrise writes, fabricated Pico master,
   implicit GPS fallback, or selection/backend coupling has slipped into delivery.
7. Review complete diffs and whitespace checks for every changed repository and
   affected component. Confirm operator docs describe only implemented behavior,
   exact units and defaults, separate saved/active state, execution owner/destination,
   and honest Pico/provider limits. Preserve attribution and recovery assets.

## Acceptance gate and report

All required checks must succeed for intended candidates. An old green CI result,
mock-only test, or failed/cancelled/timed-out/skipped required suite leaves the gate
open. Record exact commands/results, source pair, dirty changes, relevant CI links,
visual evidence, Docs build/warnings, component tests, and each repository's Git
state. Identify tests not applicable with a source-grounded reason, not a waiver.

Run the shared adversarial review loop against the integrated result and evidence.
Fix authorized in-scope defects, rerun affected checks, and repeat until no known
actionable issue remains. Report missing authority and substantive design changes
for direction instead of silently expanding scope. If repairs change a tested
contract or candidate source, invalidate affected evidence and rerun the required
checks for the final source pair, including both interface suites when applicable.
The prompt-preparation `[skip ci]` instruction does not waive these checks. Keep
Git actions separate per repository and perform them only if explicitly authorized for this execution.

Update the phase list from actual acceptance evidence, with ✅ only for satisfied
phases; preserve ⏳/▧ and an exact next unfinished step if any gate is unmet. Include
the required Documentation Impact section and limitations. Explicitly separate
hardware-free software evidence from physical USB, target timing, GPIO, RF,
installation, services, and deployment. Do not claim those unperformed qualifications
or release readiness. Stop after the authorized closeout report.
