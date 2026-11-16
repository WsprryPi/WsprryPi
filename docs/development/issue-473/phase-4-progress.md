# Issue 473 phase 4: Pi solar policy and configuration

Status: ✅ phase-4 implementation and acceptance complete. Phase 5 is not started.
The user approved P4-01, the necessary Pico companion changes, and eventual
commits/pushes in all three feature branches. No merge, hardware, installation,
service, GPIO, I²C, or RF operation is authorized or performed.

## Approved P4-01 and implemented boundary

The existing protected managed-settings sidecar remains the single authority for
solar configuration. There are no duplicate INI solar keys. Local target saves
can update a validated draft only with `solar.enabled=false` and unchanged legacy
`scheduling.enabled`. The API advertises `stored_only` for `pi-scheduling`.
The shared Control/1 schema restricts this activation value to that resource;
closed validation, generated producers/consumers, and negative vectors agree.
The control major stays 1 under the existing prerelease policy. WTP, TLS,
transport, storage formats, Pico recurrence, and legacy Pi recurrence are unchanged.

Solar activation is rejected before persistence through the API and before
configuration/output readiness on private-file recovery. INI saves, stock saves,
and reloads preserve the exact scheduling sidecar. Existing WAL publication
atomically owns INI, complete settings, receipts, and identity history. The
owner-backed runtime bridge supplies a copied ordinary/typed-solar snapshot with
its generation and revision; it cannot acquire output authority or dispatch.

## Implemented evaluator

Parent-owned `solar_geometry.*` ports the actual Pico Sun-center approximation
from `src/time/gps_calibration.cpp` at
`ef5b80924bee9d3d483d8f2fcd0c7f52a3538ea8`, retaining MIT attribution and provenance.
It supports UTC [2025,2100), uppercase 4/6/8-character locator centers, unrounded
day threshold −0.833°, lower-inclusive/upper-exclusive millidegree windows,
hour-angle morning/evening partitions, and truthful daily-extrema observations
at the sampled declination. It invents no crossing or sunrise event.

`solar_policy.*` consumes immutable configuration, proposed cursors, injected UTC,
clock/site generations, and a read-only frequency predicate. It returns typed
allowed, skipped, held, or disabled results, reasons, selected period, source
context, and nominal frequency intent. Gray windows precede independent day/night
permissions. Explicit WSPR lists have separate cursor scopes; inherited lists
share ordinary progression. Metadata, skips, profile, and band preferences survive
selection. CW uses checked exact nanohertz base/shift arithmetic and every derived
tone is validated. No evaluation advances progress, reserves ownership, prepares
selectors, samples a clock, ingests GPS, or performs I/O.

The injected clock policy bounds uncertainty at 250 ms and holdover at one hour
since a healthy sample in the same boot, growing uncertainty at 50000 ppb. Native
Pi GPS is unsupported and never falls back to a locator. Solar-off adds no
clock/location readiness prerequisite. OS clock observation, launch-time fences,
random-placement freezing, and runtime admission remain phase 6.

`solar_configuration.*` validates exact shared document shape and Pi semantics,
canonical decimal hertz, bounds, locators, complete WSPR tokens, and all changed
overrides even in disabled periods. Unchanged well-formed newly unsupported
values can be retained for repair, without granting admission. Backend checks use
Pi policy/planners or current cached WTP capability observations, not Pico limits.
New unsupported selector metadata is refused; stored metadata remains readable
when the build no longer supports GPIO. Reusable component source is unchanged.

## Review findings fixed

- Split whole/fractional Hz before integer conversion to avoid a one-nanohertz
  floating-point multiplication error; check unsigned/signed overflow explicitly.
- Preserve saved Pi FSKCW/DFCW positive-shift semantics; malformed nonpositive
  shifts hold rather than introducing an alternate CW policy.
- Return typed frequency holds on a refusing/throwing predicate.
- Guard optional Si5351 linkage and reject uncompiled backends instead of making
  a simulated build depend on an absent physical backend implementation.
- Validate unused band preferences and random-placement endpoints as well as the
  selected list; preserve the ordinary audio offset and backend correction input.
- Separate lexical selector retention from build/transport support for new edits.
- Rebuild generated Pico operator assets after changing the embedded contract;
  its stale-manifest refusal was fixed rather than bypassed.
- Refuse new WTP overrides against a changed endpoint, unready peer, or wrong
  identity; parse cached range bounds exactly and never negotiate during a save.
- A pure BCM2835 100 MHz divider-boundary reproduction exposed total versus
  additional PPM interpretation. Use the same additional-correction entry point
  as production, and cover that boundary in the full-profile solar service test.
  Restart final validation after this fix; the interrupted prior final attempt
  does not count as acceptance.
- Validate the full GPIO/RP1 four-tone CW compatibility table, not just emitted
  tones; use the actual RP1 pure planner, intrinsic baseline, and production
  dither count. Pure boundary reproductions and full-profile regressions cover
  impossible bases without overriding physical limits.
- Replace contradictory approval-pending progress text with this approved scope.

## Final validation: passed

Native pure tests, AddressSanitizer/UndefinedBehaviorSanitizer pure tests,
17,850 production Pico geometry vectors, and the focused native service/store/
startup/152-vector contract checks passed, including recovery and retention cases.
The first complete frozen source-pair run passed every requested step: network
runtime, network/configuration interoperability, both Fleet carriers, actual USB
management/UI paths, and explicit portable semantics. Final checks against the
clean committed Pico selection and final Pi candidate passed, including full
Linux semantics with hardware access disabled (the image has gpiod.hpp), the
full-profile solar service test (GPIO, RP1, and Si5351 branches), and the
standalone Si5351 pure planner. The native portable attempt found
an inherited ignored Linux executable in Chipset-Offsets; that attempt is not
counted as a pass, and the artifact was preserved.

Complete actual paired network/USB validation uses isolated frozen source copies,
selected Pico source, and Mbed TLS
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`, following the checked-in CI recipes.
Runners and exact commands: `/private/tmp/issue473-linux-pair/run_phase4_pair.py`
and `run_phase4_final.py`.
Per-step logs and source hashes are in that evidence directory; the outer log is
`/private/tmp/issue473-phase4-pair.log`. Final evidence: `phase4-final-source-manifest.json`, per-step
`phase4-final-*.log`, and `/private/tmp/issue473-phase4-final.log`. Every required step passed; the final run exited 0 and verified frozen hashes.
`phase4-final-product-manifest.json` also records source hashes excluding Python
bytecode caches; product source matched the publishing candidate. These are hardware-free software fixtures, not physical USB or RF
qualification.

## Documentation Impact

Updated operator INI/storage reference, local target API reference, and
configuration troubleshooting in WsprryPi-Docs. Sphinx builds with warnings as
errors. Impeccable review preserves the existing Read the Docs design; desktop
1440px and mobile 390px renders retain navigation and have no page overflow.
Captures: `/private/tmp/issue473-phase4-docs-captures/`. The actual Pi main plus two authenticated software Pico services also passed
real Setup browser workflows, draft isolation/resume, changed-boot refusal, and
desktop/mobile themes. Log: `/private/tmp/issue473-phase4-setup-browser.log`;
captures: `/private/tmp/issue473-phase4-setup-captures/`. An initial native attempt
used expired 24-hour cached fixture certificates; a fresh private build directory
regenerated test credentials and passed without changing production TLS. Existing
screenshots and Setup layout need no replacement because no solar UI or runtime admission is
introduced. Pico's application contract documents the narrow descriptor semantics.
The end-of-phase-6 automatic Pi pairing/TLS gate remains mandatory; remote Pi
management remains unavailable.

## Delivery boundary

Pi and Docs: `codex/issue-473-device-owned-scheduling`, starting at
`5d687133aefafa4c61fea09c1bd99c92452b4594` and
`0a9bae3dd6116b8572084510b4fa948fa8483a01`. Pico:
`codex/issue-473-phase4-paired-candidate` in the paired worktree, starting at
`ef5b80924bee9d3d483d8f2fcd0c7f52a3538ea8`; ordinary Pico checkout and earlier
paired branch are preserved. Pico is committed as `375a7fb5c5f5a79ecbde672a3521d149c3b37828`; Pi
generated provenance now pins that clean revision (`source_uncommitted=false`).
Pi/Docs phase-4 commits publish the accepted changes on the feature refs above.
The delivery report records their exact commit IDs and remote/CI verification.
Push-triggered workflows do not select these feature branches; absence of CI is
not counted as a CI pass. The unrelated untracked SQLite prompt is excluded.
No merge, phase-5 work, deployment, or physical qualification is included.

## Final assessment and reproducible commands

Repeated full adversarial assessments covered numeric bounds/precision, all Pi
backend frequency models, strict closed contracts, metadata/retention, failed
transactions, recovery/reload/copy fencing, unchanged feature-off recurrence,
no-I/O evaluation, source-pair selection, UI workflow, and operator documentation.
All findings listed above were fixed. The assessment after the final complete
run found no remaining issue. Interrupted earlier final attempts were replaced
by the successful complete run; none is represented as a passing check.

From `src`, the final runner used these focused commands with
`BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`, explicit frozen `PICO_SOURCE`, and
`MBEDTLS_SOURCE=/mbedtls` (the verified pin above):

```sh
make -j4 solar-geometry-test solar-policy-test solar-geometry-pair-test
make -j4 controller-only-test pi-managed-main-test pi-managed-wtp-main-test \
  pi-managed-host-test pi-managed-host-runtime-test pi-management-service-test \
  pi-managed-store-test pi-managed-startup-test controller-contract-test \
  wtp-fleet-test wtp-fleet-capacity-test wtp-fleet-operations-driver-build \
  pico-target-controller-test pico-management-target-test \
  pico-management-history-test pico-management-journal-test \
  build/bin/shared_setup_main_fixture
make -j4 wtp-network-runtime-test
make -j4 wtp-network-interop-test
make -j4 wtp-config-interop-test
make -j4 wtp-usb-pair-client-build wtp-usb-management-test \
  build/bin/pico_management_usb_pair_client
make -j4 semantics-test-portable
```

The same run configured the actual Pico CMake host test tree with firmware off,
built `usb_management_driver` and `endpoint_driver`, and ran shipped
`wtp_usb_settings_test.js`, `pico_management_usb_pair_test.js`,
`wtp_usb_pair_test.js` with `WSPRRYPI_GPIO_DRIVE_REQUIRED=1`, and
`fleet_operations_pair_test.js` for both network and USB. It also ran generated
contract parity, Pico lifecycle tests, and Pi UI unit tests. Exact path-bearing
commands and individual PASS results are in the final runner/logs above.

With `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, the final default Linux profile also
passed `make -j4 semantics-test SUDO=`, `make -j4 pi-management-service-test SUDO=`,
and `make -C WSPR-Transmitter/src si5351-planner-test SUDO=`. Pico's pinned npm
bootstrap/operator tests passed 27/27. Native sanitizer commands used
`CXX='c++ -fsanitize=address,undefined -fno-omit-frame-pointer'` on the two pure
targets. Operator docs passed `python -m sphinx -W --keep-going -b html docs
docs/_build/html` in the documented venv. Final whitespace checks passed.

The only modified named Pi component is `WsprryPi-UI` (generated contract data);
its unit suite and actual browser integration passed. Named reusable `src/`
component source is unchanged; existing standalone planner/clock tests ran from
their ordinary source trees in isolated copies. Hardware, service installation,
GPIO/I²C electrical behavior, physical USB, timing, and RF remain unqualified by
these software checks. No further phase-4 operator documentation is outstanding.
