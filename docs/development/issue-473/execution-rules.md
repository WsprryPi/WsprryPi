# Issue 473 prompt execution rules

These rules are part of every phase prompt. Read them and the full
[authoritative plan](../device-owned-solar-scheduling-plan.md) before executing a
prompt. The [phase list](../issue-473-phases.md) records implementation status.
The plan's product contracts, ordered slices, and validation gates remain binding;
these prompts expand execution detail without replacing those contracts.

## Authority and prerequisites

Creating, reviewing, committing, or pushing these prompts does not authorize
implementation. When a user subsequently authorizes a phase, execute only that
phase. Do not begin later phases automatically. Resolve any conflict with the
plan or approved decisions before implementation; do not silently weaken a gate.

Inspect the current branch, full working tree, staged and unstaged diffs, applicable
AGENTS.md files, affected components, and implementation contracts in each
repository used. Run `git status --short --branch`, `git diff --check`, and
`git diff --cached --check`. Preserve all existing user work. Record actual source
revisions and relevant uncommitted changes; planning hashes are not dependency pins.
For phases 2–8, verify predecessor acceptance evidence and approved phase 1
decisions against current sources. Phase 1 establishes those decisions for approval. A missing prerequisite blocks dependent work, not permission to
invent behavior. Report an exact next step and any independently safe work.

The intended application repository is WsprryPi. The plan records authorization
for concomitant WsprryPi-Docs work; verify that authorization remains applicable
to the phase being executed, locate the actual sibling repository (currently
`/Users/lbussy/GitHub/WsprryPi-Docs`), and follow its AGENTS.md. Do not create a
replacement for the historical `Wsprry_Pi_Docs` spelling. Reading a companion
checkout for contract inspection and safe fixtures does not authorize editing it.
Obtain explicit WsprryPico repository authority before required companion changes;
without it, stop the affected interface slice rather than ship a partial pair.

Do not stage, commit, push, amend, rebase, publish, create PRs, post issue comments,
or operate hardware unless separately authorized for that execution. This prompt
set's `[skip ci]` commit authorization applies only to saving these documents.
No live transmission, tones, GPIO/I2C exercises, installation, service/configuration
changes, deployment, or mutating sudo commands are authorized by any prompt here.
Reusable src components need the repository's separate scope approval before a
parent-only task expands into them. Preserve component independence and attribution.

## Approved deferred Pi security milestone

The user approved the [sequencing amendment](deferred-pi-security.md) on
2026-11-16. It narrows phase 2 to local Pi/existing Pico management and foundations;
new Pi pairing/TLS, controller-to-remote-Pi management, and dependent remote
editor/migration acceptance belong to the phase 6.2 security gate. Phases
3–6 may proceed on independently eligible paths when separately authorized.
Remote Pi management stays unavailable; no unauthenticated substitute is allowed.
The user split phase 6 on 2026-11-16: phase 6.1 is recurrence integration, and
phase 6.2 is this security/remote-management gate. Each requires separate execution
authority and acceptance; phase 6.1 completion does not authorize phase 6.2. Overall
phase 6 cannot complete and phase 7 cannot begin until both pass. Phase 8
cannot waive it. All other predecessor, security, authority, documentation, and
paired-integration gates remain binding. This changes sequencing, not the approved
security contracts, existing WTP/Pico transports, or phase-execution authority.

## Shared implementation and validation requirements

Each target owns its schedule; neither browser nor controller evaluates remote
solar recurrence or dispatches it through Fleet. Device selection is edit context,
never backend selection, enrollment, persistence, enablement, or RF ownership.
Use authenticated, bounded target adapters and strict current contracts. Preserve
identity/profile/boot/revision bindings, per-target drafts, accepted-write tracking,
and authoritative readback after uncertain writes. Do not use fallback peers,
carriers, relaxed validation, or controller sunrise writes to fill capability gaps.

For any shared interface change, inspect all producers and consumers, the current
Pico control-policy.json and checker, advertisements, descriptors, schemas,
validators, runtime, serialization, generated assets, and paired adapters. Keep
WTP, transport/authentication, and storage versions independent of settings/control.
Before the first stable transition keep the current control major, identifier,
and namespace at 1; after it follow the explicit reviewed major-change policy.
Retain one current closed control view, with no hidden downgrade or legacy branch.

Run both complete checked-in Debian workflow recipes for changed shared interfaces:
`wtp-network-loopback` and `wtp-usb-management-pair` in
`.github/workflows/debian-non-hardware.yml`. Network includes
`wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`.
USB includes the actual Pico `usb_management_driver`, `wtp-usb-pair-client-build`,
`wtp-usb-management-test`, and `WsprryPi-UI/tests/wtp_usb_pair_test.js`. Select intended
Pi/Pico candidate sources and pinned dependencies explicitly, exercising production
crypto, serialization, framing/ACK, and journals. Mocks, stale CI, failed, cancelled,
timed-out, or skipped suites do not satisfy the gate. Required companion changes
must exist on intended paired refs; distinguish local evidence from delivery.

Inspect recipes before running tests. Read `docs/simulated-backend.md` for application
simulation and explicitly select `--backend simulated`; never infer simulation
from failed hardware initialization. Use unprivileged isolated software fixtures.
From `src`, use `make semantics-test-portable SUDO=` on macOS or Linux without
libgpiod C++ development headers. On qualified Linux use
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=`. The portable subset
is not the full physical-backend semantics profile. Run focused tests suitable for
the phase, not unsafe targets merely containing the word test. Neither software
profile qualifies hardware, RF, installation, or service lifecycle.

UI implementation and affected rendered UI documentation require Impeccable:
load the skill, inspect `WsprryPi-UI/DESIGN.md` and existing patterns, preserve the
bench-instrument design, review desktop/mobile and light/dark states, keyboard and
accessibility behavior, and resolve applicable findings. Use production interfaces
for behavioral reviews. Do not commit local skill/runtime artifacts. These are
engineering prompts; preparing them does not change or render the application UI.

Review operator-documentation impact with every phase; update applicable material
concomitantly, describing implemented behavior only. Phase 8 verifies completeness
and cannot defer documentation or integration gates required by earlier phases.
For affected Docs content follow its build workflow, including activating
`docs/.venv` and running `python -m sphinx -b html docs docs/_build/html`; inspect
warnings, links, renders, and materially inaccurate screenshots.

## Required report and review loop

For the authorized phase, report changes, component boundaries, source revisions
and tested dirty changes, exact commands/results, relevant CI links, visual
workflows/evidence, documentation impact, unrun checks and limitations, each
repository's Git state, and whether commits/pushes occurred. Separate implemented,
proposed, and unresolved behavior; distinguish hardware-free evidence from physical
USB, RF, timing, installation, and service qualification. Include a Documentation
Impact section listing updated, considered-but-unchanged, and still-required docs
and reasons. Mark a phase ✅ only when its entire acceptance gate is satisfied.

Before declaring a phase complete, adversarially assess the actual diff, tests,
and documentation against its prompt, the plan, and approved decisions. Enumerate
concrete defects and missing evidence, fix authorized issues, and repeat until no
known actionable issue remains. An unmet prerequisite or unavailable required
validation stays open; review cannot turn it into a waiver. Stop at that phase's
gate and report the next unfinished step.

## Prompt preparation review record

This records review of instructions, not implementation qualification. Each prompt
was drafted, challenged, corrected, and reassessed in order on 2026-11-15.
The final pass for each must find no known actionable prompt defect; phase 1
behavior/security decisions remain deliberately unresolved until approved.

| Phase | Initial review finding and correction | Reassessment |
| --- | --- | --- |
| 1 | Added exact enable/save decision coverage and separated recommendations, contract changes, and repository authority from approval. | No known actionable prompt defect after reassessment. |
| 2 | Added explicit origin/CSRF, proxy/redirect, revocation, discovery authorization, and no-mutation failure tests. | No known actionable prompt defect after reassessment. |
| 3 | Removed ambiguity about the future Scheduling tab and protected access/semantics during existing-editor migration. | No known actionable prompt defect after reassessment. |
| 4 | Added typed nonfatal holds and tests that API/file/reload storage cannot activate unfinished policy or bypass fencing. | No known actionable prompt defect after reassessment. |
| 5 | Separated recurrence handover from manual/WTP permissions and prohibited resolving unknown jobs by reset or evidence deletion. | No known actionable prompt defect after reassessment. |
| 6 | Added uncertain-progress recovery/crash coverage, same-frequency policy disagreement, and measurable wake/write bounds. | No known actionable prompt defect after reassessment. |
| 7 | Required independent target execution evidence during controller absence and server-side validation beyond disabled UI fields. | No known actionable prompt defect after reassessment. |
| 8 | Required warning disposition and invalidation/reruns of affected evidence after candidate repairs; confined skip-CI authority to prompt preparation. | No known actionable prompt defect after reassessment. |

Final cross-prompt review checked phase order, prerequisite gates, scope/authority,
plan consistency, documentation timing, candidate-pair validation, status semantics,
and relative links. Corrected the shared prerequisite wording so phase 1 does not
require its own decisions to be approved before design begins. No known actionable
prompt defect remained. This is a bounded
review finding, not proof that future implementation or unresolved designs are safe.
