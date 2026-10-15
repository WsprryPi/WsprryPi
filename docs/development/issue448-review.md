# Issue 448 implementation and adversarial review

Date: October 15, 2026. Scope: [#448](https://github.com/WsprryPi/WsprryPi/issues/448)
and the paired software acceptance gate in
[#447](https://github.com/WsprryPi/WsprryPi/issues/447).

The [execution prompt](issue448-execution-prompt.md) was written before
implementation on `codex/issue-448-compatible-config-updates`, from Pi
`8454e21429c89401b1b9fb5f47a5e15276b86763`. Both local devel branches were
fast-forwarded before starting. Pico's reviewed dependency is merged at
`87144d1364f36d04bfe0a714bd1c2f6deb974302`.

## Result and boundaries

The settings form uses a host-coordinated GET/POST settings transaction with
fresh member identity, exact revision and actual remote update capabilities.
Modern peers receive one targeted merge; older peers receive one complete,
explicitly understood legacy candidate that retains observed supported fields.
Unknown fields, unsupported advertised capabilities and malformed advertised
application state refuse before mutation. Existing direct resource compatibility
and schedule-only writes remain. This introduces no configuration store,
scheduler, management carrier, full replacement UI or hardware editor.

The UI sends changed fields, retains drafts/passwords after failed or uncertain
writes, requires explicit saved-state readback before another Save and never
replays a mutation automatically. Feedback follows confirmed restart state.
See [the contract](../wtp-configuration-compatibility.md) for schemas, budgets,
legacy exclusions and recovery semantics.

Modified component boundaries: `WsprryPi-UI` and the parent runtime/management
adapter `src/wtp_integration`. No reusable `src/` library component was modified;
WTP-Client and WSPR-Transmitter remain unchanged. Pico sources are read-only exact
fixtures. No native macOS C++ compilation, tool installation, SDK download,
firmware deployment, service change, physical output, GPIO or RF test occurred.

## Actual request and persistence matrix

| Pi producer | Clean Pico source | Evidence |
| --- | --- | --- |
| Unchanged `8454e21429c89401b1b9fb5f47a5e15276b86763` | `87144d1364f36d04bfe0a714bd1c2f6deb974302` | Actual old Load/Save handler emits legacy PUT without pins/flags; Pico retains nondefault pins, both true flags and password through journal reload |
| Updated | `95aac22bf7cc31b67fe5773761708d743d8a6473` | Strict legacy fallback contains only supported fields; password null preserves; one mutation and authoritative reload |
| Updated | `dfffdd5220db8b7cf1acf5e74950dbd294283c55` | Extended pre-fix legacy replacement explicitly retains known pins/flags; scoped schedules, resets and saved/active pin latch |
| Updated | `87144d1364f36d04bfe0a714bd1c2f6deb974302` | Advertised POST merge contains intended changed leaves and exact member target; scoped schedules, explicit false/reset, pin latch and journal-generation revisions |

Final matrix status: all four rows passed after the adversarial repair.
The unchanged producer and updated producer both pass against the current pin;
the updated producer also passes the strict and extended historical pins.

The actual JavaScript producer runs in a Node DOM fixture and its requests pass
through actual Pi WtpBrowserApi, configuration coordinator, WtpApplication idle
management, outbound HTTP/TLS, unmodified Pico server/API and the Pico journal
with deterministic RAM flash. A separate driver supplies the runtime callback;
this is not a complete installed browser/proxy/service path. Actual runtime mutex
and pending-state protection are tested separately in wtp_production_test.

The unchanged producer is the Pico repository's committed historical fixture;
its SHA-256 is verified as
`08639d74049de9c9f66890423bd339cc86eaf328bce9a9004a42aeaec628f963`.
Every source and pinned Mbed TLS checkout is required clean at configuration.
The existing TLS dependency is
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`; no SDK is fetched.

Each updated row checks station/Wi-Fi/password/expiry/enable/time-server saves,
explicit enable/time-server resets, schedule-only draft
preservation, unchanged cursor/password digest, wrong targets, stale writers,
commit followed by a deliberately discarded client reply, one mutation without
retry, explicit readback retaining the credential draft, and failed RAM-flash
commit with unchanged authoritative configuration/cursor. Extended/current rows
also check false/reset values, saved RF pin 2 versus active pin 28 with pending
restart, and A-to-B-to-A revision rejection. The old strict ETag cannot detect an
external ABA sequence; that guarantee is explicitly excluded for its row.

## Commands and results

C++ validation uses existing GCC 14.2/OpenSSL 3.5.7 on Debian ARM64. All
application validation is hardware-inhibited. Commands below run from `src`
unless stated otherwise; source variables identify the exact clean revisions
above and `MBEDTLS_SOURCE` the retained pinned checkout.

- Baseline `8454e21`: `make wtp-api-test SUDO=` passed.
- `make wtp-config-test wtp-api-test SUDO=`: passed after the repair.
- Focused ASan/UBSan: passed after the repair, with no sanitizer diagnostics.
- `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j2 wtp-api-test backend-http-guard-test wtp-protocol-test wtp-plan-test wtp-usb-test wtp-backend-test wtp-scheduler-test wtp-status-test wtp-application-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` passed.
- `make -j2 wtp-network-runtime-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=` with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`: passed (277 production runtime checks).
- `make wtp-network-interop-test SUDO= WTP_NETWORK_BUILD_DIR=build/wtp-network-current PICO_SOURCE="$PICO_CURRENT" MBEDTLS_SOURCE="$MBEDTLS_SOURCE"`: passed against the refreshed current pin.
- Current matrix: `make wtp-config-interop-test SUDO= WTP_NETWORK_BUILD_DIR=build/wtp-network-current PICO_SOURCE="$PICO_CURRENT" MBEDTLS_SOURCE="$MBEDTLS_SOURCE"` passed before the final malformed-context repair. The final run rebuilds `wtp-config-client-build`, configures/builds each exact profile and runs the same actual-producer driver again.
- Historical profile configuration: `cmake -S tests/network -B "build/wtp-network-$profile" -DPICO_COMPAT_PROFILE="$profile" -DPICO_SOURCE="$PICO_SOURCE" -DMBEDTLS_SOURCE="$MBEDTLS_SOURCE"`, then `cmake --build "build/wtp-network-$profile" -j2` and `node ../WsprryPi-UI/tests/wtp_configuration_interop_test.js "$PWD/build/wtp-network-$profile" "$profile" "$PICO_SOURCE"`. Profiles are `legacy` and `extended`; they use the identical rebuilt Pi client executable. Both passed before the repair; final status is recorded above.
- `npm test` from `WsprryPi-UI` passed, including PHP/template, existing UI/source regressions and the new actual-producer unit test.
- `WSPRRYPI_UI_CAPTURE=0 node tests/wtp_network_ui_integration_test.js` from `WsprryPi-UI` passed all rendered workflow assertions using existing PHP and Chrome, retaining previous image evidence.
- `php -l WsprryPi-UI/data/views/wtp-controls.php`, JavaScript syntax checks and final Git whitespace checks passed.

Sanitizer reproduction:

```sh
make wtp-config-test wtp-api-test SUDO= \
  WTP_NETWORK_BUILD_DIR=build/wtp-config-sanitize \
  WTP_NETWORK_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_NETWORK_LDFLAGS='-fsanitize=address,undefined'
```

Unit/adapter checks include strict duplicate/depth/byte bounds, malformed HTTP
status/header responses, unknown configuration fields, unsupported capability,
missing/stale revisions, response races, wrong boot, owned/loaded/armed/running/
faulted/unconfirmed output, malformed mutation replies and failed delivery.
The review repair adds missing advertised application fields, wrong nested
types/targets, unhealthy storage and complete valid modern context. Rejection
cases assert zero mutations. The production fixture exercises actual operation
mutex contention and refuses management during a pending request without WTP I/O.
The component runs passed 10,373 protocol, 2,942 session, 1,088 execution-plan,
2,714 USB CDC (fakes/sysfs/PTYS), 5,410 backend, 1,514 scheduler, 3,804
status/recovery and 1,337 application checks. Existing runtime/interop tests cover
the retained lifecycle, credential rotation,
network transaction, cancellation, recovery and shutdown paths.

Initial validation problems were corrected rather than reported as passes:

- The real current Pico serializes optional `indicator_active_high`; the first
  integration run correctly exposed that omission in Pi's understood pin shape.
  It is now preserved and regression-tested.
- Current Pico's server constructor, PSA lifetime and credential validator require
  fixture/source-list changes. The exact committed sources are compiled directly.
- The strict source guard rejected a clean but newer concurrent Pico docs commit;
  the test checkout was detached at the reviewed canonical revision.
- An overlapping fixed-port TLS fixture could not initialize. The existing
  interop suite completed; the final matrix ran sequentially after it exited.
- The first sandboxed npm run could not bind a loopback socket; the authorized
  local rerun passed. Two late browser fixture assertions depended on a draft
  replaced by new explicit Load scenarios; the preconditions were restored and
  the final assertion-only run passed.

## Impeccable and rendered UI

Impeccable's harden workflow was used before and during implementation, with
PRODUCT.md, DESIGN.md and existing Bench Instrument controls as the design
reference. Existing sidecar drift was reported and left unchanged. No design
system or unrelated presentation work was added.

Reviewed rendered 1280x900 desktop and 390x844 mobile states in light/dark themes:
settings/schedule Save, password preservation, readback hints, stale conflict,
unsupported settings, uncertain result and disabled management. Inline status
announcements remain accessible, control grouping/wrapping is retained and the
browser asserts no horizontal overflow. Busy availability text explains disabled
controls. Blank-password and restart feedback use the established local hints.

Images are generated under `src/build/wtp-network/ui/` and remain build artifacts.
Accepted evidence includes `desktop-schedule-wtp-management-feedback.png` (light,
credential retained/schedule result), `desktop-dark-settings-saved.png`, and
mobile light/dark settings stale/unsupported/ambiguous/busy images. Some desktop
state captures framed the Fleet selector despite a passing DOM visibility check;
those files are excluded as evidence of settings feedback. No further visual
polish was warranted. A mistakenly launched additional capture run repeated the
same fixture failure; the final functional rerun explicitly disabled capture.
The fixture's disconnected controller banner is mocked state, not a live-device
observation. No installed UI/runtime or physical carrier qualification is claimed.

## Adversarial review and closure

An independent agent reviewed the complete implementation, first-party UI,
source pins, fixture changes, CI, contracts and evidence limits. Its first source
assessment found no actionable issue. The final challenge found one P2:

- **Incomplete advertised application projection accepted:** schema/target/ETag
  alone allowed a save without establishing required saved/active/restart state.
  The coordinator now validates the complete known projection, nested schemas,
  targets/types/generation/hash forms, healthy storage and application-error
  state before mutation. Negative tests prove zero writes. Older peers without
  the advertisement still have unavailable/null context. Independent reassessment
  accepted the repair and found no additional actionable issue.

Final evidence reassessment: source repair accepted with no remaining actionable
findings. The final focused, sanitizer and all four real matrix runs passed; the
independent evidence review is recorded in the integration issue.

## Documentation Impact

Updated the Pi browser API, this execution/review record, the compatibility
contract and the production-coverage page's pointer to current evidence. Historical
production records remain historical. Pico dependency documentation was reviewed;
no companion implementation edit is needed here.

Read the separate WsprryPi-Docs instructions and relevant setup/backend pages.
That repository is outside this authorized write boundary and was not changed
or built. Operator follow-up is required under
`docs/User_Interface/Setup/Transmitter/index.md#pico-development-controls`: document
standalone settings preservation/older-peer refusal, the ordinary editor's lack
of replacement/reset controls, blank-password behavior, schedule-only draft
preservation, saved versus active/restart feedback, and explicit readback after
conflict/unconfirmed results. Review the linked
`docs/Advanced_Operations/ini_configuration/transmitter_backends.md#pico-wtp` for
consistency. No screenshot replacement is justified by this bounded change alone.

These software results do not establish release readiness, firmware/hardware,
physical flash, installed proxy/auth/services, electrical or RF correctness.
Issue #423 and unrelated release/fleet work remain outside scope.
