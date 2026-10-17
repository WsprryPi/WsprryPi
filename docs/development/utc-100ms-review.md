# Accepted 100 ms UTC policy: implementation and adversarial review

Date: 2026-10-17. Executed on `devel` against WsprryPi base
`d43b4e305519fa4e1612c42c293849647d357e5c` and WsprryPico base
`e8b5fb53691526c9cd5370fb334843fd6124220b`. The complete coordinated
[prompt](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/utc-100ms-execution-prompt.md)
and [Pico result](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/utc-100ms-results.md)
retain scope, firmware enforcement and physical-evidence boundaries.

## Delivered behavior

This is a product configuration/default change, not a harness override.
`WtpSettings` now supplies 100000000 ns (100 ms); missing-field JSON parsing
uses that constructed default. Generated public configuration, INI loading,
new catalog entries, discovery drafts and UI defaults inherit it. USB, Plain
LAN and TLS share this policy. Existing explicit 1 ms, 37 ms, 250 ms and other
valid values remain exact. Reads, imports, unrelated saves and profile renames
do not silently increase them. There is no migration based on guessing whether
a saved 1 ms value was intentional.

The active form labels the value **Maximum UTC uncertainty (ns)**, explains
100000000 ns = 100 ms, and keeps Setup's existing autosave for deliberate field
edits. Operators enter the new value and review the Setup save status. The
fleet editor additionally has **Use 100 ms**, which changes only its draft.
**Save device** persists the profile; **Use this device** applies that profile
to the active host endpoint. Custom values remain editable.

The real application/scheduler/backend sends the selected value in ARM. An
18 ms scripted target admits with the new default, refuses with an explicit
1 ms setting, and a target above 100 ms refuses before ARM. Backend boundary
checks cover 99999999, 100000000 and 100000001 ns. Pico separately enforces the
per-job value at ARM and local launch, including drift after ARM. 100 ms is an
estimated UTC admission bound, not a measured error or symbol/edge jitter limit.

No USB time measurement accounting, Pico firmware engine, WTP/1 or management
framing, host UTC qualification, clock-step/leap guard, drift/holdover rule,
lead/submission reserve, missed-start deadline, cancellation/recovery guard,
capacity, RF memory reserve or foreground service budget changes. Pico's
advertised 500 ms maximum and standalone policy remain unchanged. USB name,
configuration and Wi-Fi CRUD and existing standalone/network paths remain.

## Validation

All WsprryPi C++ compilation/linking used retained Linux Docker images, never
the unsupported native macOS path. `wsprrypi-item16-gcc13:local` supplied the
native runtime/application builds; `wsprrypico-p153-routing:local` supplied
Node/OpenSSL/loopback and USB-pair checks. Containers used `--network none`;
loopback-only fixtures are distinct from remote networking or hardware.

From `src`, with `BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`:

| Check | Outcome |
| --- | --- |
| `make wtp-backend-test` | 5608 checks passed, including default/strict admission and actual serialized ARM value. |
| `make wtp-application-test` | 1337 checks passed. |
| `make wtp-catalog-test` | Passed import/reopen, missing-field add, rename/reopen, explicit old-value preservation and existing discovery/consent cases. |
| `make build/bin/wtp_production_test`; `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./build/bin/wtp_production_test` | 629 checks passed; repeated after review cleanup. Includes fresh/public defaults, three transports, missing-field INI, explicit-value INI round trips, unrelated saves, real worker admission/refusal and serialized ARM policy. |
| `make wtp-usb-management-test` | Passed framing, ACK, identity, replay, qualified time/error bounds, cancellation/deadline and protected browser guards. |
| `make wtp-usb-pair-client-build`; `node ../WsprryPi-UI/tests/wtp_usb_pair_test.js /work/src/build/wtp-network/wtp_usb_pair_client /pico/build/host-linux-usb/usb_management_driver` | Actual production Pi/Pico management/time handlers passed via fragmented anonymous pipes: name, Wi-Fi create/replace/delete-last/recreate, saved progress, station configuration and UTC; no hardware/network opens. Companion driver/source unchanged, so the dedicated prior source pin is retained. |
| `make wtp-plain-tcp-test` | Resolver, loopback I/O, configuration and TLS-isolation cases passed. |
| `make wtp-tls-test` | Actual TLS probes reached the contract phase; an old unreadable rotation fixture blocked the first contract run. Retried the unchanged binaries/test with a fresh ephemeral fixture directory: credential/settings contracts and all 34 actual TLS cases passed. |
| `node WsprryPi-UI/tests/wtp_ui_test.js` from repository root | Passed default/custom-value population, unrelated changes, visibility, validation, reload and draft preservation. |
| `node WsprryPi-UI/tests/wtp_usb_ui_integration_test.js` | Rendered actual PHP/JS at 1440x1100 and 390x844, light/dark; profile preset emits no mutation, explicit save carries 100 ms and reloads, active form payload contains 100 ms. Existing USB/no-network UI checks also pass. |
| `php -l WsprryPi-UI/data/views/wtp-controls.php`; `node --check WsprryPi-UI/data/wtp.js` | Passed. |
| Impeccable mechanical detector on the two changed presentation files | Empty finding list, including after the persistence-guidance repair. |
| `git diff --check` | Passed. |

Pico's full 186-entry host suite initially had two failures and one optional
schema skip. The new test incorrectly expected physical output from its
inhibited engine; this assertion was repaired and the profile test passed.
The unrelated Phase 14 localhost relay was sandbox-blocked; it passed with
loopback permission. The schema case passed separately using the already
installed qualification-harness Python environment. Thus every suite entry
has passing evidence, with the original interpreter skip retained honestly.
Focused Pico profile tests also pass under AddressSanitizer/UndefinedBehaviorSanitizer.

UI artifacts are regenerated under ignored
`src/build/wtp-network/ui-usb-progress/`, including eight policy screenshots
and `checks.json`. Human inspection covered both surfaces, widths and themes;
no overflow, clipped policy controls, focus or contrast issue was found in the
changed area. A second batched round verified the corrected active-form copy.
An intermediate browser assertion used a nonexistent helper name; it was
corrected to the actual `buildConfigPayload` and the complete workflow reran.
Native runtime logs also contain expected unavailable-device-tree and injected
failure diagnostics; these are hardware-inhibited tests, not live faults.

The TLS retry copied `wtp_tls_probe` and `wtp_tls_contract_test` into the
container's fresh `/tmp/utc-100ms-tls/` and ran
`python3 -B tests/wtp_tls_test.py /tmp/utc-100ms-tls`. The original stale fixture
was left intact; no credentials or generated files are included in this commit.

## Adversarial assessment and closure

These are in-thread adversarial self-assessments, not an independent review.
No agent or external reviewer was represented as having reviewed this change.

First assessment traced construction -> JSON -> INI -> catalog -> browser ->
application -> scheduler -> serialized ARM, and challenged persistence and
clock boundaries. It found one actionable UI defect: the proposed active-form
preset said to save host settings, but Setup uses autosave and has no separate
Save action. That could leave a draft unapplied or cause adoption during an
unrelated later edit. Closed by removing that active preset and explaining the
existing explicit field-edit/autosave behavior. The fleet-only preset retains
its real Save/Use workflow. UI tests/docs/prompt were corrected and affected
checks rerun. The inhibited-engine assertion and stale TLS fixture were test
expectation/environment issues, retained above rather than hidden.

Fresh second assessment re-read the final product/test diff, searched all live
default consumers and remaining 1 ms occurrences, checked exact preservation
and invalid-value rejection, actual ARM serialization, host and local-launch
boundary enforcement, fleet busy/hidden behavior, input event/autosave wiring,
profile save versus active use, protocol pins and unchanged engine/capacity
source. No actionable findings remain. Historical 1 ms fixtures and prior
investigation statements are intentionally retained with explicit supersession
links. No timing or RF qualification was inferred from mocks.

## Documentation Impact

Updated production configuration, network examples, Pi operation, USB management
and the previous USB review's supersession note. Pico updates the investigation
and USB management policy explanation and records this execution separately.
The normative WTP protocol and standalone scheduling contract need no change:
they already enforce explicit per-job bounds and retain existing ceilings.

Reviewed the separate `/Users/lbussy/GitHub/WsprryPi-Docs` read-only at
`5f539fbcac1e02b0edb58c017479105647975b27`. Required operator follow-up:

- `docs/Advanced_Operations/ini_configuration/transmitter_backends.md`: replace
  the 1 ms default, explain preserved saved values and deliberate 100 ms adoption.
- `docs/Command_Line_Operations/transmitter_backends.md`: replace the 1 ms
  default-budget statement with the 100 ms ordinary default and strict-value caveat.
- `docs/User_Interface/Setup/Transmitter/index.md`: explain the active autosave
  versus fleet Save/Use workflow and update applicable field screenshots.

That third repository is outside this two-repository write scope. Existing USB
management claims there also predate the coordinated USB feature; reconcile
those in the same operator-documentation follow-up, not by modifying this code.

## Physical and publication scope

No Pico flash, RF ARM, RF transmission, Pi service, host clock or installed
application was changed in this policy pass. A new physical USB/inhibited ARM
acceptance was optional and was not run. Earlier qualified USB clock estimates
(17.686–18.235 ms) are historical and remain distinct from the deterministic
100 ms policy tests and unqualified absolute RF start accuracy. No 1 ms
physical capability is claimed. Commit/push identifiers and fresh CI state
are reported after publication; no in-progress CI run is called passed.
