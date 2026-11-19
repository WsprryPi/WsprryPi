# Phase 6.1: Pi device-owned recurrence

Status: ✅ implementation and software acceptance complete. Phase 6.2 remains not started.

The user authorized phase 6.1 implementation, review/fix loops, commits, pushes, and monitoring enabled CI. No merge, later-phase implementation, or hardware operations are authorized. The phase 6.1 prompt and approved D01–D12 govern this work. Preserve the unrelated untracked SQLite feasibility prompt.

## Inspected baseline

- Pi: `1b9cbf952c5990a6204a69ab1fe331d310202d45`, feature branch `codex/issue-473-device-owned-scheduling`.
- Paired Pico: `f7c4fad280c811755b2e1619ae90f438eb06d49b`, clean candidate worktree on `codex/issue-473-phase5-paired-candidate`.
- Phases 1–5 are recorded complete; current code retains the phase-5 local authority fence. Solar activation is still refused, scheduling capabilities advertise `stored_only`, and status is an explicit unknown placeholder.

## P61-01: required companion contract scope — approved

Materialize phase 6.1 activation and runtime status in the authoritative Pico Control/1 schema, descriptors, strict validators, shared vectors, generated consumers/assets, and engineering documentation. Keep the current prerelease major, identifier, namespace, WTP, and storage versions at 1. Reuse the already approved target settings/status structures and exact save/readback bindings. No new Pico scheduling feature, transport, pairing, hardware operation, or phase 6.2 work is included. Validate both complete production network and USB source pairs. Required Pico edits, commits, and pushes need fresh phase-specific repository authority under the saved execution rules. The user approved this scope, including required companion commits and pushes, on 2026-11-16.

## P61-02: reusable transmitter launch boundary — approved

Affected component: `src/WSPR-Transmitter`. Its canonical request already contains a scheduled slot, but local WSPR `TransmissionScheduler` independently computes a new window; it does not pin that slot through final launch. There is also no application-supplied generation/admission callback at final launch. Parent-only checks cannot close this gap or prevent a prepared occurrence from moving to a different unevaluated window after delay or a clock step.

Proposed narrow change: honor an explicitly bound canonical occurrence slot in the existing scheduler, refuse rather than advance a missed bound slot, and add a typed optional final-launch guard with explicit refusal/cleanup semantics. Leave ordinary unbound scheduling and manual/one-shot/tone/WTP authorization unchanged. Keep callbacks application-neutral; introduce no application headers, solar policy, JSON, persistence, or WTP dependency into the component. Add focused standalone component tests, update its README, and run parent simulated lifecycle and complete carrier suites. Repository component policy requires approval before this parent task changes a reusable component interface. The user approved this narrow component scope on 2026-11-16.

## Implemented candidate

The existing WSPR and repeating QRSS/FSKCW/DFCW paths evaluate immutable native policy before selector work, ownership, or commitment. Both WSPR starts must select identical policy identity and complete intent. The accepted complete pair or message freezes its realized tones, placement, and future slot. Final local launch and outgoing WTP CLAIM/ARM retain admission, generation, and cleanup checks. No parallel scheduler, new WTP message, Pico scheduling feature, or security workflow is introduced.

Protected enable/disable actions use the existing closed Control/1 documents, exact candidate digest, revision, device/boot binding, and durable replay receipts. Scheduling disable drains accepted work independently of Transmit and boot policy. Immediate Stop remains separate. Local ownership is never released by a solar wait or disabled scheduling. Native rollback requires disabled, reconciled work and retains established progress through subsequent staging.

A fixed progress-initialization proof is published with first native authority, settings, and receipt under the main version-1 WAL. Its digest remains bound into settings after rollback or staging. Seven fixed registered destinations do not increase the six-image transaction bound. A separate bounded coordinator owns the dynamic progress record: one reserve and one terminal publication per admitted occurrence, one per explicit skip, and none for polls, forecasts, or policy holds. Unknown acceptance requires positive output reconciliation and is consumed conservatively. Missing or inconsistent established history remains held.

Linux clock health uses read-only kernel observations, acquisition uncertainty, bounded same-boot holdover, and generations for steps, suspend, or health changes. Solar-off recurrence adds no time-health/site requirement. Other platforms without an approved OS health provider report solar admission held. Forecasts remain bounded to 48 hours and 4,096 candidates; ordinary recomputation is at most 1 Hz.

## Adversarial assessment and fixes

Review/fix iterations corrected:

- double-counted holdover growth, synchronization-loss versus alarm handling, fresh health after provider loss, and acquisition skew;
- selector idle synchronization before policy admission, complete-pair identity checks, exact tone conversion, explicit skip timing, and independent cursors;
- progress owner replacement during settings saves, native rollback, missing history after repeated handover, and terminal storage uncertainty;
- queued callbacks reaching replacement work, stale launch/configuration guards, positive output reconciliation, and refused-launch active-state cleanup;
- missing protected enable/disable route classification and configuration reservations held during immediate native preparation;
- delayed monitor notifications cancelling already applied work. Only an identical protected INI file identity and content may retain admission; replacement closes it;
- generated Pico operator bundles requiring regeneration after contract changes, GCC integer inference in a test, and the focused Linux test profile;
- repeated ordinary preparation/logging while native scheduling or Transmit is off; disabled polls now retain reconciliation without acquiring configuration reservations or publishing progress;
- forecasts retaining consumed or refused slots; terminal, faulted, and recovery-held observations now report an unknown next start;
- missing header prerequisites on the new focused test targets;
- an unreadable mobile API table, replaced with endpoint descriptions.

The focused native suite passes with actual planner and simulated workers, 384 state/boot combinations, complete-pair refusal before GPIO test events, QRSS/FSKCW/DFCW overrides, WSPR execution, callbacks, independent/inherited lists, skips, refused commitments, file replacement, idle preparation refusal, and bounded writes. The actual-main protected API test passes automatic initialization, enable/disable, accepted drain, restart retention, and interrupted-reservation reconciliation without replay. Expanded management tests cover enable replay, rollback, retained progress, and missing progress/proof holds. Final paired and semantics evidence is recorded below.

## Documentation Impact

Updated five operator pages in the separately authorized Docs repository: configuration, runtime/boot semantics, protected APIs, troubleshooting, and Operations. They describe current API access and explicitly retain the future Scheduling editor boundary. Existing screenshots remain representative because no new editor is included. The reusable transmitter README documents the typed slot/guard and callback boundary. Pico engineering documentation describes the coordinated current Control/1 contract; the control major remains 1.

Sphinx builds without warnings. Impeccable render review covers five pages at 1,440-pixel desktop and 390-pixel mobile widths, both OS color preferences, and checks page overflow. Captures were inspected for readable text, hierarchy, wrapping, and links. The existing theme remains light under dark OS preference; no theme redesign is included. Evidence: `/private/tmp/issue473-phase61-docs-render/` and `/private/tmp/issue473-phase61-docs-review.log`.

## Final validation and acceptance

All 19 final frozen-source stages passed. The runner copied the intended sources, recorded their hashes, and verified that the source files remained unchanged. The final working and staged Pi/Pico files matched those hashes before delivery metadata was updated. Baselines are listed above; all phase-6.1 uncommitted changes were included. MbedTLS was clean at `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.

Evidence: `/private/tmp/issue473-phase61-final.log` and per-stage logs under `/private/tmp/issue473-linux-pair/phase61-final-*.log`. Source manifest: `/private/tmp/issue473-linux-pair/phase61-final-source-manifest.json`, SHA-256 `25c8849cb94e97734c9c4804d3874edc7f11a09e6eeb66529f53eb40767270fc`. Runner: `/private/tmp/issue473-linux-pair/run_phase61_final.py`, executed in `wsprrypi-rc3-validation:local` with network disabled, physical access disabled, unprivileged UID 501, and read-only candidate mounts. Its frozen source root was `/work/issue473-frozen/phase61-candidate-review-1794881303782812625`.

Linux commands ran from that root's `pi/src`, with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, explicit `PICO_SOURCE=<frozen-root>/pico`, and `MBEDTLS_SOURCE=/mbedtls`. Exact expanded commands and results are in the runner log. Required commands included:

- `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= solar-clock-test recurrence-progress-test native-recurrence-test native-recurrence-main-test scheduled-launch-test wspr-plan-placement-test recurrence-authority-record-test solar-geometry-test solar-policy-test solar-geometry-pair-test` — passed.
- `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= wtp-network-runtime-test` — passed, including 916 production runtime checks.
- `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= wtp-network-interop-test` — passed, including actual second-address TLS rebind, scheduled launch, occurrence guards, and lost-reply recovery.
- `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= wtp-config-interop-test` — passed, including current production resources, journals, crypto, strict contracts, and independent browser/controller contexts.
- `cmake -S <frozen-root>/pico -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` and `cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4` — passed.
- `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= wtp-usb-pair-client-build wtp-usb-management-test build/bin/pico_management_usb_pair_client` — passed.
- `WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js <frozen-root>/pi/src/build/wtp-network/wtp_usb_pair_client /tmp/pico-usb/usb_management_driver` — passed. Independent USB management and both network/USB Fleet pair stages also passed.
- `make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= semantics-test-portable` — passed; this is the explicit portable subset.
- `make -j4 SUDO= semantics-test non-wspr-repeat-policy-test band-gpio-rotation-test selector-shutdown-cleanup-test` — passed with the full Linux backend profile and hardware access disabled.

Generated-pair checks, Pico policy tests, UI unit tests, application build, and all parent management/service/store/journal/startup/authority regressions also passed. The standalone `src/WSPR-Transmitter/src` command `make scheduled-launch-test BACKENDS=simulated SUDO=` passed through its parent target. Added test-header dependencies were checked with the focused targets and `make -n -W solar_policy.hpp -W wtp_integration/pi_management_journal.hpp`.

Clean macOS source copy: `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -C /private/tmp/issue473-phase61-macos/src -j2 semantics-test-portable SUDO=` — passed for the final code. Log: `/private/tmp/issue473-phase61-macos-semantics.log`. Loopback access was needed for its HTTP/WebSocket fixtures. A supplemental macOS actual-network run passed with Clang's generated-schema overlength diagnostic retained as a warning; macOS cannot bind the second IPv4 loopback alias, so that supplemental case was skipped. The mandatory Linux network run exercised and passed that actual address change.

The actual shared Setup browser regression passed with two candidate Pico TLS services, the production Pi fixture, desktop/mobile layouts, both themes, retained drafts, changed-boot refusal, and isolated saves. Evidence: `/private/tmp/issue473-phase61-shared-browser.log` and `/private/tmp/issue473-phase61-shared-setup-captures/`. Impeccable review inspected these captures and the five rendered operator pages. Sphinx build and render checks passed; no phase-6.1 operator-documentation follow-up remains.

## Final adversarial assessment

After each finding, the affected checks were repeated and the final source pair was frozen again. Two final complete assessments covered admission before effects, pair identity/freezing, S/P/T/boot independence, ownership, launch/callback races, progress initialization and recovery, bounded writes/wakeups, truthful forecasts, closed current contracts, generated consumers, component boundaries, and operator workflows. No further actionable findings remained. Both required carrier suites and both Linux semantics profiles have successful final-source results; no failed, cancelled, or skipped mandatory check remains outstanding.

No physical USB, installation, service lifecycle, GPIO/I²C, timing, or RF qualification is claimed. Production solar clock health is Linux-only under D06; other platforms hold solar recurrence. These are documented boundaries, not software acceptance claims for physical operation.

## Delivery boundary

Authorized delivery uses Pi and Docs branch `codex/issue-473-device-owned-scheduling` and paired Pico branch `codex/issue-473-phase61-paired-candidate`. The validated Pico content is committed as `2fdf078a5e379fd9fb329745150ca5005f39ad04`; operator documentation is `04ad4a7cd4884881bc1fa61f163278d341bcc366`. Pi contract provenance was refreshed from that clean Pico commit without changing generated code or schema content. Only the approved files are staged; the unrelated SQLite prompt remains untracked and excluded. Final commit IDs and post-push CI observations are reported in the completion message. Overall phase 6 remains in progress; phase 6.2 and phase 7 have not started. No merge is authorized.
