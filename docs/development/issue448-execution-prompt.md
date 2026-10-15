# Issue 448 execution prompt

Implement [#448](https://github.com/WsprryPi/WsprryPi/issues/448) in WsprryPi,
from `8454e21429c89401b1b9fb5f47a5e15276b86763`, on isolated feature branch
`codex/issue-448-compatible-config-updates`. Local Pi and Pico devel have first
been fast-forwarded. The settled Pico contract is merged in
`87144d1364f36d04bfe0a714bd1c2f6deb974302`; review its complete changes before
updating the canonical integration pin.

## Scope and implementation

Modify the first-party WsprryPi-UI component and the parent runtime's
`src/wtp_integration` management adapters, their tests and documentation. The
defect crosses actual UI request production, serialized management and outbound
HTTP; a UI-only omission fix cannot provide safe negotiation or fallback.
Preserve component boundaries and their independent tests. Do not change WTP/1,
RF engines, provisioning, recurrence authority, carrier support or unrelated
release/fleet work. No Pico source change is planned or authorized by this
implementation plan; companion sources are clean read-only pinned fixtures.

Introduce a host-scoped settings transaction at GET/POST
`/api/v1/member/configuration`, schema `wsprrypi-pico-settings/1`. This is a Pi
management coordinator, not another Pico configuration resource or store.
Browser requests still pass trusted-interface, authentication, same-origin,
CSRF/JSON-intent and bounded-body rules. Its runtime callback executes inside
the existing operation mutex and `WtpApplication::idle_management`; never open
a bypass connection during owned, queued, active or unresolved output.

GET reads actual remote capabilities, saved Config and status, verifies current
device/boot from the freshly negotiated WTP identity, and checks a consistent
config ETag before returning a redacted context. When advertised, read the
existing application projection to verify target and saved/active/restart
reporting. Host feature projections and WTP CAPS are never update capabilities.
Return the selected target, saved config, actual remote update metadata,
strategy and available restart state. Missing/malformed/oversized/duplicate
responses, absent identity, unsupported configuration shapes or inconsistent
revisions refuse safely. Never echo credentials in errors.

POST requires exactly `schema`, `target`, `operation`, `config`, with the exact
loaded If-Match. `operation` is `save` or `schedules`. Target is exactly
`{scope:"member",device_id,boot_id}`. Re-read authoritative context under the
same serialized authority, compare identity and revision, validate a nonempty
closed subset of understood Config v1 fields, then perform at most one mutation:

- Existing configured Pico advertising supported `transmitter-config-update/1`,
  POST `/api/v1/config/update`, merge mode and usable byte budgets: send only
  intended changed fields in its targeted merge envelope.
- No update capability: construct a full Config v1 from the authoritative
  understood saved shape plus intended edits. Retain present supported pins,
  flags, expiry and time-server values; omit absent optional extensions rather
  than sending them to a strict old parser. Refuse unknown fields or attempts
  to introduce unadvertised extensions whose support cannot be established.
- Present but unsupported/malformed capability: refuse; never downgrade after
  a possibly applied mutation or guess support from version/date or probing.
- Initial configuration: use existing full PUT with required base fields and
  explicit credentials. Do not use an update route requiring saved state.
- Schedule-only: retain one scoped PUT of the complete schedule array. Keep
  unrelated station/Wi-Fi/credential drafts and settings intact.

Respect 2,048-byte host/new-update requests, advertised limits and 1,800-byte
full legacy candidate. Arrays replace; known station/Wi-Fi leaves merge;
password null preserves and requires saved state. Explicit false, expiry zero,
time-server resets and complete pin plans are intentional values. A complete
replacement/reset mode is not exposed by this ordinary editing workflow.

Retain direct legacy resource API compatibility and finite route/method
allowlists. Extend outbound HTTP only for POST config/update and GET application;
POST must carry the same authenticated Host/Origin, JSON-intent and exact
If-Match protections as PUT. Do not add PATCH or a consumer/USB/plain-LAN JSON
carrier. Distinguish every pre-mutation read from the one allowed mutation.

Treat any malformed successful mutation reply, dropped response, post-commit
failure or unknown outcome as unconfirmed; never replay, retry, switch strategy
or substitute endpoint automatically. Preserve server error categories needed
for operator recovery without reflecting arbitrary response secrets.

## UI workflow and Impeccable

Use the Impeccable harden workflow during implementation. Preserve the existing
Bench Instrument design, controls, layout and inline live feedback. No redesign.

Load the host-scoped authoritative context. Normal Save produces only changed
form fields, with one atomic transaction; blank password preserves and explicit
password clears only after confirmed settings-save success. No-change Save
reports that fact without mutating. Schedule-only Save preserves every unrelated
draft. Keep advanced frequency/pin controls outside this form.

Conflict, stale context and unconfirmed mutation invalidate write context and
require explicit saved-state readback. Preserve drafts and entered password
through that readback, show saved values beside affected controls for review,
then allow a deliberate new Save. Normal explicit Load may replace a draft;
status polling never does. Bind every write to the loaded device/boot/revision.
Never store credentials in browser persistence or logs.

Use actual response/application restart semantics: distinguish confirmed saved,
pending network/pin activation, later admissions without reboot and unknown
restart state. Do not say every save needs reboot. Preserve accessible busy,
disabled and status announcements. Do not claim profile-owned identity changes
were saved when Pico rejects them.

Render desktop/mobile in light/dark themes using existing PHP/Chrome and mock
responses. Inspect Load/Save, safe legacy/unsupported support, stale and
ambiguous writes, busy/disabled, password preservation and restart feedback.
Use one batched visual inspection and at most one confirmation pass after
fixing findings. Commit no runtime/skill artifacts or synthetic credentials.

## Acceptance and exact version pairs

Use isolated Linux ARM64 on authorized wspr5, existing tools/dependencies and
loopback adapters only. No macOS C++ compilation, incidental installation or
SDK downloads. Do not use the disconnected external SSD. No hardware/RF test
is required for these software contracts; keep such qualification separate.

Keep exact clean companion checkouts and existing pinned Mbed TLS
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Retain these separate rows:

1. Unchanged Pi `8454e21` producer to new Pico `87144d1`: preserve newer settings.
2. Updated Pi to old strict Pico `95aac22bf7cc31b67fe5773761708d743d8a6473`:
   safe legacy PUT with only supported fields and password-null semantics.
3. Updated Pi to extended pre-fix Pico
   `dfffdd5220db8b7cf1acf5e74950dbd294283c55`: preserve understood extensions
   in explicit legacy candidate, without crediting server omission preservation.
4. Updated Pi to new Pico `87144d1364f36d04bfe0a714bd1c2f6deb974302`:
   advertised merge, exact target/revision, authoritative journal reload.

Capture actual UI Load/Save requests and route them through actual Pi browser,
compatibility and outbound HTTP adapters into actual Pico handler/server and
journal; mocks alone or hand-built requests cannot close the matrix. Keep
the original producer as a provenance-bound fixture. Verify authoritative
reload, nondefault pins/flags, password redaction and cursor retention.

Test ordinary station/Wi-Fi/expiry/enable and schedule edits, explicit resets,
new/absent/unsupported capabilities, unknown future shapes, missing context,
wrong device/boot, revision races, stale and ABA writes. Record the old strict
server's weaker legacy ETag/ABA semantics as an exclusion rather than claim
modern guarantees there. Modern Pico must retain generation/ABA protection.

Cover malformed/duplicate/oversized/unsupported input and response, busy and
owned/loaded/armed/running/faulted output, pending network/restart transactions,
pin-restart latches, interrupted saves and dropped responses after commit.
No rejected edit may mutate or clear STOP, faults or cursor.

Review the new Pico source before pin/source-list refresh. Use current core
sources and existing link boundaries without editing companion fixtures to
hide incompatibility. Keep old and extended pins in separate coverage, including
CI checkouts. Validate relevant targets from src: `make wtp-api-test SUDO=`,
`make wtp-network-runtime-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`,
`make wtp-network-interop-test SUDO= PICO_SOURCE=... MBEDTLS_SOURCE=...`,
focused compatibility/sanitizer checks and suitable semantics regressions.
Run WsprryPi-UI unit and browser tests. Inspect target recipes before execution.

## Review, documentation and publication

Write `issue448-review.md` with the matrix, exact commands/results, baseline
failures/skips, visual evidence and boundaries. Update `docs/wtp-browser-api.md`
and affected production/compatibility evidence, not unrelated release gates.
Read the separate WsprryPi-Docs instructions and relevant transmitter setup/
backend pages; report exact operator follow-up without writing that repository.

After implementation and validation, obtain an independent adversarial agent
review of the complete diff, contract, matrix and evidence. Record each
actionable finding; repair, rerun affected checks and request another assessment
until none remain. Challenge dropped unknown fields, credential drafts,
capability confusion, wrong-target/stale writes, weaker old-server semantics,
ambiguous replay, false success and unsupported compatibility claims.

Review complete/staged component and parent diffs, commit attributable changes,
push feature and verify remote SHA. Merge to refreshed devel preserving
concurrent changes, push/verify, assess CI, fast-forward clean local devel,
then delete local/remote feature branch and task-owned worktree. No force push
or release. Update #447/#448 with source-only evidence; close only fulfilled
gates and retain operator documentation/physical limitations explicitly.

Report behavior, durable prompt/review, component paths, exact tests/results,
independent findings/repairs, UI/Impeccable evidence, Documentation Impact,
source-pair coverage, limitations, commit/merge/parity/CI, branch deletion and
actual clean/dirty local state.
