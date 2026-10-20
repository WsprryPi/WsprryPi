# Item 16: WsprryPi 4.0 Beta 1 preparation

Execution scope agreed October 19, 2026. Work on `devel`; preserve unrelated
changes. The user authorized execution, adversarial repair/reassessment, and a
commit and push. They explicitly selected Pi editor adoption of Pico's completed
independent station saves, and reuse of recorded physical evidence. No Pico
source change, live installation,
service operation, GPIO/RF activity, or release publication is authorized.

The user subsequently authorized the prepared operator Docs patch, its build/
render review, and a separate commit/push on `WsprryPi-Docs/devel`.

## Objective and stop boundary

Execute release punchlist 16a–16c as far as the accepted inputs and evidence
permit. Produce comprehensive, sourced Markdown prerelease notes, an auditable
evidence ledger, a frozen candidate/source/artifact inventory, validation and
adversarial assessments, and an exact remaining-gate handoff. Never mark an
unperformed check or unaccepted limitation complete. Stop before 16d–16e: no
release-branch merge, tag, GitHub release/draft, public asset upload, issue
announcement/edit, or final stable-release claim.

## 1. Orient with a code and history review

1. Inspect branch, full staged/unstaged/untracked state and applicable AGENTS.md
   instructions. Verify the independent `origin/devel` head before writing.
2. Review capabilities and changes since the previous published stable release,
   especially backends, configuration, WTP, Pico management and USB firmware
   maintenance, Fleet, support/security, installation and build contracts.
3. Read items 13–16 and their acceptance records. Reconcile stale checklist
   statements with source and commits without rewriting historical evidence.
4. Identify actual implementation gaps relevant to the selected scope and
   record them. A review is not unrestricted codebase/hardware qualification.

## 2. Adopt completed Pico station-progress behavior in Pi

1. Use Impeccable, existing design tokens and interaction patterns. Preserve the
   complete atomic configuration/station APIs and older-firmware workflows.
2. Extend the existing USB station-progress workflow to the selected supported
   engineering HTTPS, consumer station/AP, and USB management carriers. Keep
   name/Wi-Fi USB controls and existing drive-progress behavior intact.
3. Discover the exact additive `saved_progress` capability. Missing discovery
   leaves complete saves available; malformed/future discovery refuses safely.
   Do not infer support from a firmware date, WTP CAPS or host feature flag.
4. Use the current member/device/boot, management selection and common ETag;
   retain operation locking, trusted-LAN/Origin/JSON-intent policy, idle and
   storage admission. Do not require a complete station to save valid fields.
   Retain Pico-owned profile/setup authority and all existing wire schemas.
5. Save station fields independently. Omission, explicit null, false and zero
   keep their established meanings. Invalid fields and edits made during an
   in-flight request remain visible. Blank power means unset, never zero or an
   invented default. Display per-field outcomes near the corresponding inputs.
6. Applying saved station progress is a separate deliberate action, using the
   device-saved revision. Clearly distinguish drafts, applied configuration,
   boot-active hardware, restart requirements, STOP and transmit readiness.
7. Validate remote readbacks and acknowledgements, including bounded JSON,
   closed shapes, identity, uint64 strings, receipts, request attribution and
   per-field outcomes. Lost/malformed/stale replies invalidate all write
   contexts and require explicit read/review. Never retry mutations, substitute
   carriers, replay subsets, or store credentials/drafts in browser persistence.
8. Advance only the network interoperability reference to the accepted P15.9
   software revision. Retain the previous P15.8 reference as `p158`, preserving
   every historical row (nine network compatibility rows total). The separate
   current USB reference remains unchanged. Pico source is read-only.
9. Exercise the actual UI producer through protected Pi routing/coordinator
   and unmodified pinned Pico handlers on all supported carriers. Cover partial
   valid saves, null/zero, invalid siblings, explicit application, profile
   refusal, uncertainty/review, target changes, storage faults and legacy peers.
   Render desktop/mobile, light/dark and zoom; inspect keyboard and feedback.

## 3. Research and draft 16a

1. Query current GitHub releases; identify the previous published non-draft,
   non-prerelease stable tag and peeled source commit. Preserve the entire
   stable-to-candidate commit range, including post-checklist changes.
2. Enumerate all issues/PRs, labels, milestones and visible project associations
   with complete pagination. Research every ready-for-release issue, every
   closed issue in the range and historical issues referenced by range commits.
   Record inaccessible metadata precisely; never infer a private project state.
3. Read candidate issue bodies/comments and reconcile them with actual commits,
   merges, source, component READMEs, acceptance records and operator guidance.
   Classify included, partial, superseded/duplicate, excluded, deferred and
   unresolved. Neither closure nor a readiness label proves implementation.
4. Write `release_tools/4.0-Beta-1-Release-Notes.md` as the eventual public body,
   and `release_tools/4.0-Beta-1-Release-Evidence.md` as its claim/issue/commit/
   validation map. Retain machine-readable issue/source/artifact inventory where
   useful. Do not expose private bench paths, credentials or target captures in
   the public notes.
5. Cover additions, changes, fixes, removals/deprecations, real compatibility
   breaks and uncertain risks, defaults, APIs/WTP, configuration, platform and
   backend boundaries, installation, dependencies, upgrades and rollback,
   qualification limits and known issues. Include explicit operator actions;
   distinguish software support from measured RF/hardware qualification.

## 4. Freeze/build 16b and assess 16c

1. Select the intended `4.0.0-beta.1` prerelease identity using existing version
   conventions. Preserve truthful commit/branch/dirty build attribution. Record
   any tagging/version-embedding gap instead of fabricating a published tag.
2. Record exact Pi source, operator Docs, Pico network and USB references,
   immutable RP1 software/supplement locks and independently updateable installer
   contract/channel. Do not mistake a test pin for a firmware image pin.
3. Build/checksum candidate exports with checked-in Linux ARMv6/AArch64 Bookworm
   and Trixie recipes. Do not compile/link C++ locally on this Mac. Keep large
   binaries/build output out of Git; retain manifests, digests and build evidence.
4. Run relevant component/parent tests and exact-source CI. Preserve failures,
   intentional skips and platform limitations. Inspect targets before running;
   containers must have no hardware devices or production service access.
5. Apply item 10's installation/runtime matrix. Identify which historical
   evidence remains reusable by unchanged inputs, which has changed, and which
   checks remain unperformed under the user's no-live-operation selection.
   Do not call a fresh candidate accepted if required checks lack evidence.
6. Inspect the sibling operator Docs read-only. Record exact coverage and
   required follow-up paths; cross-repository changes/publication are separate.
   Update application-side contract/developer docs within this scope.

## 5. Adversarial assessment and closeout

Challenge omitted issues/features, unsupported release claims, stale identities,
pin/history loss, incomplete pagination, API downgrade/fallback, confused
draft/applied state, late responses/new edits, null/zero, malformed success,
lost-write receipt attribution, profile/storage/idle gates, cross-editor revision
reuse, secrets, UI accessibility, build provenance and evidence reuse.

Repair actionable in-scope findings, repeat affected checks, then perform another
adversarial assessment. Record findings and closure evidence. Report external
acceptance gates honestly rather than hiding them as review success.

Review the complete staged diff and final whitespace checks. Commit only this
scope on `devel`, push `origin/devel`, independently verify remote parity and
inspect final-head CI. Report outcomes, prompt/notes/evidence paths, validation,
Documentation Impact, remaining gates, working-tree state and commit/push.
