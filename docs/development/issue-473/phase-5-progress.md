# Issue 473 phase 5: authority interlock and explicit handover

Status: ✅ software acceptance complete. Phase 6 is not started.
Commit/push delivery and remote CI observation follow acceptance.
User-authorized delivery is commit/push after phase-5 acceptance, with enabled CI
monitored. No live migration, hardware, RF, service, installation, or deployment
operation is authorized. Preserve the existing deferred-security planning edit
and untracked SQLite prompt; exclude both from phase-5 commits.

Starting sources: Pi `63a597f30e1c0ff9ee91498925699e908ac76e31`, Docs
`88a3cd4a3622507ddd69f32feebe834f55c98413`, both on
`codex/issue-473-device-owned-scheduling`; paired Pico
`375a7fb5c5f5a79ecbde672a3521d149c3b37828` on
`codex/issue-473-phase4-paired-candidate`. Phase-4 full/portable semantics,
actual network/USB, geometry, persistence, and browser evidence passed. Current
phase-5 work uses `codex/issue-473-phase5-paired-candidate`; its generated consumer
records the edited source until the companion commit establishes the clean pin.
The ordinary Pico checkout stays separate.

## P5-01: required shared schema companion scope

The approved phase-1 normative design already defines target migration/readback/
rollback, authority/controller/retirement/job rows, and their exact guarded
bindings. Those definitions are absent from the authoritative Pico Control/1
schema and generated consumers. Add those approved shapes and narrowly necessary
capability/route registration, strict validators, generated assets, vectors,
test adapters, and engineering documentation together. Keep the one current
prerelease Control/1 major/namespace and all WTP, carrier, and Pico recurrence
behavior unchanged. No new Pico scheduling or pairing feature is proposed.

This needs phase-5 Pico edit/commit/push authority; phase-4 companion permission
was phase-specific. Use the existing paired feature worktree, preserve the
ordinary checkout, pin its clean resulting revision in Pi, and run both complete
production network/USB suites for the intended pair. No shared interface is
implemented before companion authority is supplied.

## Inspection findings and safety boundaries

D07 explicitly withdrew persistent closure of the shared Plain LAN claim lane.
Keep normal Transmit-off/fresh-WTP-claim semantics; fence retired saved Fleet
assignments through durable controller tombstones and target-local migration
state, not changed WTP identity or claims. An unavailable old-controller management
path remains pending. Remote Pi management and its dependent handover integration
remain unavailable until the approved end-of-phase-6 security gate.

Current Fleet pause/removal cleanup journals are not migration retirement proof:
resolved entries are removed and pause can be resumed. Assignment v1 has no
persistent assignment ID/tombstone. The target service reports authority
`unclassified`; no target-authoritative complete prior-controller inventory or
never-used installation attestation exists. Empty catalogs, absent files,
revocation generation zero, and idle output cannot supply those missing facts.
These gaps require explicit inspection/resolution before any native eligibility,
clean-install success, or automatic migration can be claimed.

Independent work may inventory and implement parent-owned durable authority
validation/fencing with fail-closed unknown defaults. New recurrence stays
unavailable; no phase-6 admission or phase-7 enable UI is added. No reusable
component edit is included without separate approval.

## Documentation Impact

The phase indicator, this engineering record, Fleet/browser/Pi operation guides,
and bounded-tone behavior are updated. The authorized operator-docs branch covers
Maintenance initialization and migration, existing-output retirement, pending-state
troubleshooting, and the local API. Sphinx builds and Impeccable desktop/mobile
reviews passed. Existing repair imagery remains accurate in its Repair section;
new UI states were inspected in actual application captures. No reusable component
README requires a change. Final paired validation passed, as recorded below.

P5-01 was approved by the user for the required phase-5 companion changes.

## P5-02: recurrence authority in the existing atomic owner (approved)

The current v1 WAL's `authority` destination is the device catalog/history anchor,
not the independent recurrence-authority record. Do not repurpose that image or
silently reinterpret retained v1 recovery decisions. The user directed retaining
`pi-management-transaction/1`. Add a separate `recurrence` image kind to the one
current schema, preserving the meaning and recovery of existing records.
Keep the six-image transaction bound; normal target transactions can
then include INI, settings, receipts, catalog history, recurrence authority, and
later progress. Pairing retains its separately owned transaction boundary.
Publish the recurrence record, disabled settings, and receipt under one durable
decision, preserving all prior rollback and missing/corrupt-history holds.
This is a parent-owned storage-format change, independent of Control/1 and WTP;
Fleet assignment v1 remains unchanged. Add crash/recovery tests for existing
records and transactions containing recurrence authority; reject unknown
versions/kinds. No historical version branch or downgrade fallback is proposed.
P5-02 is approved with version 1 retained; the separate image is implemented.

## P5-03: automatic installation initialization (approved)

The original clean proof required a target-owned installation/attestation record
and explicit trusted local confirmation of a never-used output. Current managed
history and revocation records do not distinguish a genuinely first-used current
identity from upgraded/wiped unknown history. Ordinary startup, missing files,
empty catalogs, discovery, and a browser assertion alone must not create proof.

The user approved replacing the historical never-used-hardware claim with a
record of this installation having accepted no work since its authority record
was established. Establish that record automatically before admission; explicit
setup on the **Maintenance** page saves validated initial configuration and
stages scheduling disabled. The action is named **Set up this transmitter**.
Users manage no files, keys, certificates, or proof tokens. Updates and ordinary
configuration resets preserve installation history. Existing history requires
migration; missing or damaged established history requires recovery. Completely
erased hardware cannot prove previous-controller retirement, and fresh external
Plain-LAN WTP claims remain governed by unchanged WTP/D07 rules. No permanent
exclusion of unknown old controllers is claimed. This approval supersedes the earlier provisioning-only proposal; no external
attestation service is required.

## Foundation implementation and first review (historical checkpoint)

The companion now uses `codex/issue-473-phase5-paired-candidate` from the clean
phase-4 pin. Approved migration request/readback/rollback definitions and bounded
controller/retirement/job rows are materialized in the single current Control/1
schema. Readback and rollback have separate internal validation families while
retaining the exact approved wire `schema: target-migration` and method/path
separation. No migration capability or route is advertised as operational yet.
Generated Pi/Pico assets and exact descriptor closures agree, including the new
migration descriptor. This is contract materialization, not a working handover.

Parent-owned `recurrence_authority.*` validates the exact independent private
record, identity/generation/row bounds, duplicate identities, and strict bounded
JSON. Its initial value is unclassified with no clean proof. Shape validity is
explicitly not evidence of eligibility or verified retirement. It performs no
I/O, writes, ownership acquisition, or dispatch. The focused record target is
included in the existing full and portable semantics profiles.

First adversarial review found that the C++ schema matcher lacked array
`uniqueItems` enforcement and that JS duplicate comparison depended on object-key
ordering. Both were fixed; migration-specific duplicate assignment identities
also refuse conflicting revisions. Added authoritative negative vectors for
unknown versions/states, numeric/zero generations, unbounded/duplicate inventory,
caller-supplied proof, rollback field injection, and conflicting assignment rows.

PASS: native focused record tests; ASan/UBSan record tests; 165 authoritative C++
and JS document vectors; Pico lifecycle-policy tests; generated-source parity;
whitespace checks. Logs: `/private/tmp/issue473-phase5-record.log`,
`issue473-phase5-record-sanitized.log`, `issue473-phase5-contract-final.log`,
`issue473-phase5-controller-js.log`, and `issue473-phase5-pico-policy.log`.
The foundation frozen actual-pair regression passed through
`/private/tmp/issue473-linux-pair/run_phase5_foundation.py`; outer log
`/private/tmp/issue473-phase5-foundation.log`. No phase-5 acceptance is claimed.

P5-02 and P5-03 are approved, including version 1 and Maintenance placement.
Implementation and assessment are in progress. Still required: complete durable
target ownership and controller tombstones,
complete verified inventory/provenance, generation fences on all recurrence paths
without blocking manual/one-shot work or draining accepted pairs incorrectly,
protected migration/rollback workflows, production crash/lost-reply tests,
operator docs/rendering, complete final intended-source paired validation, and
repeated final assessment before commits/pushes. Phase 6 remains untouched.

## P5-04: protected retirement on the old controller (approved)

D07 approves retiring an old controller's own assignment from its protected UI,
while authenticated cross-controller retirement belongs to the deferred security
gate. The exact local browser route and its durable proof book were not defined.
The existing Fleet Remove/Pause paths are insufficient: their cleanup records
are erased after reconciliation, assignment IDs are not persistent, and deleting
a recovery file would reopen a restored v1 assignment.

Propose the following current Control/1 interface, with no version increment:

- `GET /api/control/v1/host/retirements` returns a closed, bounded
  `controller-retirements` document identifying this controller and at most 32
  existing assignments with server-issued assignment IDs, revisions, generations,
  and `legacy|retiring|retired|rollback|held` state. It reveals no connection
  credentials, file paths, or request bodies.
- `POST /api/control/v1/host/retirements` accepts a closed
  `controller-retirement` intent containing contract/schema, controller ID and
  current incarnation, operation/transition ID, target ID, assignment ID,
  expected assignment revision, and expected retirement-book generation. Require
  the existing protected local browser session, CSRF, Origin/Host/LAN checks,
  and strong revision header; never accept caller-supplied retirement proof.
- The existing-assignment editor offers **Retire this controller's schedule**
  with explicit review. Persist its generation fence before further dispatch,
  drain/reconcile accepted work without silently cancelling a WSPR pair, retain
  the frozen assignment and unknown work, and report retired only after positive
  readback. Offline/unknown output remains pending. Normal Pause/Remove cannot
  erase this fence. This does not claim the target's schedule was migrated.
- Keep a bounded device-identity-owned retirement book, independent of the INI
  filename and unchanged assignment v1. Establish it atomically before Fleet
  workers start, anchor missing/corrupt-history refusal, and coordinate assignment
  changes and proof publication through the existing v1 transaction machinery.
  Retired saved rows cannot resume after restart, restoration, or alternate INI
  selection. Only an explicitly target-verified rollback may release a fence.
- Keep the prescribed authenticated machine retirement interface unavailable
  until its security gate; no browser-only receipt substitutes for a scoped
  authenticated proof to another controller. WTP and pairing are unchanged.

Validation must cover all dispatch/resume/reconnect/file-load paths, direct API
bypass, replay, concurrent intent, lost replies, reboot and crash points, bound
capacity, genuine network/USB production pairs, and protected browser review.
The user approved P5-04. Implement and validate this local interface while
retaining the independent security gate for authenticated machine retirement.

## Implementation and review checkpoints (historical)

Local Pi startup now establishes source-bound installation provenance before
admission, using atomic private directory publication. Explicit Maintenance
initialization saves validated settings with Transmit, boot enable, and scheduling
disabled. Established missing/corrupt history holds recovery. Normal legacy startup
records the local authority without conversion. Migration/rollback bind device,
boot, configuration/capability revision, complete controller set, and generation;
authority, staged settings, frozen legacy configuration, and receipts share WAL v1.
Accepted work drains, old-boot uncertainty needs explicit reconciliation, and
manual/one-shot/WTP authorization stays separate. Native new enablement is absent.

P5-04 is implemented as an independently checksummed device-owned book, persistent
assignment IDs, and the protected local browser route. A stable installation anchor
outside the book directory and its current image/generation share the same v1 WAL
decision on every transition, including first establishment. Missing established records, stale individual images, anchors, or whole directories cannot be
recreated as new permissions. Retirement fences CLAIM/LOAD before persistence and
allows ARM only for the exact LOAD handed off beforehand. Accepted work drains;
fresh matching idle/unowned readback confirms retirement. Offline/unknown work
remains pending with original intent, frozen row, observation, and receipt retained.
Saved assignment flags are cleared only after proof, as a monotonic follow-up;
the book remains authoritative if that write fails or an old enabled row is restored.
New Fleet assignment creation refuses before context/transport construction.

Review fixes include strict duplicate/tuple validation, stable private-path binding,
retained positive initialization proof, descriptor/route security registration,
bounded read-only retry of transient observation capacity, exact post-restart
receipt recovery, private publisher descriptor ownership and staging lock,
independent tombstone anchoring, coherent retired Fleet actions, and avoiding a
retirement fsync wait in accepted-job ARM admission. Browser review corrected a
fixture that had interacted with hidden tab content; current evidence exercises
the visible Fleet tab. The TCP concurrency fixture preserves 24 reads and priority
stop while bounding handshakes to the server's five-connection listen backlog.

PASS checkpoints include actual initialization/crash recovery, native migration,
accepted-pair drain/reconciliation/rollback, retirement restart and real WAL crash
boundaries, generated contracts/negative vectors, UI unit checks, actual Maintenance
and retirement browsers, and the interim frozen Debian management/Fleet suite.
These checkpoints do not replace final-source acceptance. The full review-pair run
found a transient observation-capacity refusal in a fixture that assumed every
read succeeds immediately; it now retries only exact typed read-side busy results,
never mutations. Full paired network/USB and full/portable Linux semantics must
pass for the final intended sources. The final review loop and commits/pushes
remain outstanding. No physical hardware, RF, installation, or service operation
has occurred; phase 6 remains untouched.

Further review closed strong-context ETag coverage of live assignment revisions
and controller incarnation, exact original-header replay (including missing v1
rows), and definite pre-write conflict versus uncertain-publication handling.
The browser requires a fresh review after a definite refusal and retains unknown
requests; confirmed fences cannot briefly reopen the editor. Test Tone cleanup
cannot restore retired recurrence, while manual/one-shot authorization remains
independent. The actual controller HTTP pair fixture now supplies its typed
private-state namespace before startup, rather than relying on a test UID's home.
Final required pair validation remains pending after these fixes.

The next concurrency pass identified the interval between the enqueue fence and
its durable retirement decision. The worker now leaves that interval pending;
it cannot infer retirement, clear saved assignment flags, or publish completion
from the fence alone. A typed pre-publication fixture holds that interval while
accepted work drains and verifies the book remains legacy and saved flags stay
unchanged until the decision is persisted. The final-source paired gates are
being repeated after this fix. Browser confirmation uses native keyboard Space
activation and verifies focused controls; initialization, migration/rollback,
and lost-reply retirement passed in the actual simulated application. Operator
troubleshooting now explains pending work and retained history; the repair image
belongs to Repair, not initial setup.


## Final acceptance and adversarial assessment

All final-source steps below passed. The complete review after the last fix
rechecked startup and reload admission, every direct mutation and dispatch path,
accepted-job draining, retirement publication ordering, identity/boot/revision
binding, exact replay, read-only unknown recovery, crash/restart history,
rollback, independent manual/one-shot/Test Tone/WTP permissions, closed schema
parity, browser drafts and focus, and rendered operator guidance. No further
issues were identified. Phase 6 has not started; remote Pi management and its
dependent authenticated handover remain at the approved security gate.

The intended pair was Pi baseline `63a597f30e1c0ff9ee91498925699e908ac76e31`
plus the reviewed phase-5 changes, and Pico baseline
`375a7fb5c5f5a79ecbde672a3521d149c3b37828` plus the reviewed companion changes.
The frozen source manifest is
`/private/tmp/issue473-linux-pair/phase5-complete-pair-source-manifest.json`,
SHA-256 `c6e38e449a969556c04fb367810e7f70d16fa65da6490c61440c75f9ae1d8359`.
Pinned Mbed TLS is `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`.
The final runner verified every frozen source hash after execution; a separate
comparison verified current application/Pico source parity. Final commits and
clean generated provenance preserve those tested semantics.

Run on Debian ARM64 in `wsprrypi-rc3-validation:local`, unprivileged, with
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, no network, no capabilities, and isolated
executable temporary storage. `PICO_SOURCE` explicitly selects the edited paired
candidate, and `MBEDTLS_SOURCE=/mbedtls` selects the pinned dependency. Full
semantics uses installed libgpiod C++ headers and the default physical-plus-simulated
profile with hardware access disabled; portable semantics is its explicitly
separate simulated subset. No physical hardware, installation, service, timing,
or RF qualification is claimed.

Exact stage commands (run from the frozen Pi `src` directory) and PASS results:

```text
generated-pair python3 /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/scripts/generate_control_contract.py --source /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico --check
pure make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls recurrence-authority-record-test solar-geometry-test solar-policy-test solar-geometry-pair-test
pico-policy python3 /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico/tests/control_policy_tests.py
ui-unit npm --prefix /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/WsprryPi-UI run test:unit
application-build make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls debug
parent-management make -j1 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls controller-only-test pi-managed-main-test pi-managed-wtp-main-test pi-managed-host-test pi-managed-host-runtime-test pi-management-service-test pi-managed-store-test pi-management-journal-test pi-private-network-persistence-test pi-managed-startup-test controller-contract-test test-tone-lifecycle-test wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build pico-target-controller-test pico-management-target-test pico-management-history-test pico-management-journal-test build/bin/shared_setup_main_fixture
network-runtime make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls wtp-network-runtime-test
network-interop make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls wtp-network-interop-test
config-interop make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls wtp-config-interop-test
network-fleet node ../WsprryPi-UI/tests/fleet_operations_pair_test.js /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/bin/wtp_fleet_operations_driver network /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/wtp-network/pico_tls_server /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/wtp-network/credentials-v3
usb-settings node ../WsprryPi-UI/tests/wtp_usb_settings_test.js
usb-driver-config cmake -S /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico -B /tmp/pico-usb -DWSPRRY_PICO_BUILD_FIRMWARE=OFF -DWSPRRY_PICO_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
usb-driver-build cmake --build /tmp/pico-usb --target usb_management_driver endpoint_driver -j4
usb-client make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls wtp-usb-pair-client-build wtp-usb-management-test build/bin/pico_management_usb_pair_client
independent-usb node ../WsprryPi-UI/tests/pico_management_usb_pair_test.js /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/bin/pico_management_usb_pair_client /tmp/pico-usb/usb_management_driver
usb-pair env WSPRRYPI_GPIO_DRIVE_REQUIRED=1 node ../WsprryPi-UI/tests/wtp_usb_pair_test.js /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/wtp-network/wtp_usb_pair_client /tmp/pico-usb/usb_management_driver
usb-fleet node ../WsprryPi-UI/tests/fleet_operations_pair_test.js /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/bin/wtp_fleet_operations_driver usb /tmp/pico-usb/endpoint_driver /work/issue473-frozen/phase5-complete-pair-1794871873902201427/pi/src/build/wtp-network/credentials-v3
portable-semantics make -j4 BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls semantics-test-portable
full-semantics make -j4 SUDO= PICO_SOURCE=/work/issue473-frozen/phase5-complete-pair-1794871873902201427/pico MBEDTLS_SOURCE=/mbedtls semantics-test
```

Full output: `/private/tmp/issue473-phase5-complete-pair.log` and
`/private/tmp/issue473-linux-pair/phase5-complete-pair-*.log`.
All actual network runtime/interop/configuration, network Fleet, actual Pico USB
driver/client/management/production pair, USB Fleet, parent management, schema
policy, UI unit, pure authority, geometry, full semantics, and portable semantics
stages passed; none of the required carrier stages was skipped or substituted.
The current shared vectors contain 209 controller cases and 22 recovery cases.
Earlier failed checkpoint runs are superseded only by this successful final pair.

Additional actual-browser commands passed, using the explicitly isolated compiled
production main (each command begins with the shared environment below):

```sh
env WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 \
  WSPRRYPI_MANAGED_MAIN_FIXTURE=/private/tmp/issue473-phase5-native/bin/pi_managed_main_fixture \
  WSPRRYPI_INITIALIZATION_UI=1 node WsprryPi-UI/tests/configuration_recovery_browser_test.js
# Same command with WSPRRYPI_MIGRATION_UI=1, then WSPRRYPI_CONTROLLER_RETIREMENT_UI=1.
# The flags select isolated fixture scenarios, not production application controls.
```

Current captures are in `/private/tmp/issue473-phase5-{initialization,migration,retirement}-screenshots-final`.
Reviewed desktop 1440px and mobile 390px, light/dark, setup/read-only recovery,
migration review/staging/rollback, retirement review/unknown/pending, native
keyboard Space activation and focus, near-action feedback, and no horizontal
overflow. The existing Fleet browser regression also passed, including retained
drafts, current editor/revision conflicts, capacity, keyboard confirmation,
late replies, and bfcache. Browser fixtures reported expected unreachable update
checks under isolated networking; initial held startup can report authority-pending
catalog observations. Those are not carrier or save failures. Negative fixtures
intentionally report corrupt-history/startup refusals and unavailable physical
model paths; they retain state and do not initialize hardware.

Documentation validation passed:
`source docs/.venv/bin/activate` followed by
`python -m sphinx -b html docs docs/_build/html` in WsprryPi-Docs.
`node /private/tmp/issue473-phase5-docs-render.js` verified desktop/mobile
Maintenance initialization/ownership, Setup retirement, troubleshooting, and API
pages, loaded images/navigation, and no page overflow. Impeccable review found no
remaining applicable issues. PHP lint for the three changed PHP files, browser
fixture syntax, and final whitespace checks passed.

Changed parent areas are the first-party `WsprryPi-UI`, application lifecycle and
scheduling, `src/wtp_integration`, and `src/wtp_endpoint`; no named reusable
`src/` component was modified. The new pure authority test is independently
buildable, and UI units run through the component's existing npm entry point.
WTP and existing TLS libraries are unchanged; Control/1 and the independently
owned WAL remain version 1. The operator guides and engineering contracts above
are current. No additional phase-5 documentation is outstanding; installation,
services, physical USB, timing, and RF remain separately qualified activities.

Clean paired companion commit: `f7c4fad280c811755b2e1619ae90f438eb06d49b` on
`codex/issue-473-phase5-paired-candidate`. Regeneration and `--check` passed;
only provenance metadata changed from the tested generated consumer, and all
Pico source hashes match the successful final frozen pair.
