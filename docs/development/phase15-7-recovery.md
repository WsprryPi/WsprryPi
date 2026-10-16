# P15.7 native login and save recovery

WsprryPico leads Consumer-Recovery/1. Pi adopts its fresh password proof through
the existing runtime operation lock, admission/selection identity and management
transport. It adds no scheduler, transmitter owner, automatic reconnect/retry,
carrier fallback or reusable WTP-Client change.

Current Pico implementation pin is
`26ae596b540e92f25da37f978321ba1ec5934bdc`. The source guard requires that exact
clean history-bearing checkout; the published P15.6 source
`792b5b2f60e381a6bd8e7a7726eeffe24fc08519` is preserved as explicit `p156`.
The six historical rows remain legacy, extended, p152, p153, p154 and p156;
current tests P15.7. Frozen management/1 and /2 and Radio/1 bytes are unchanged.
See the [lead design, protocol and evidence](https://github.com/WsprryPi/WsprryPico/blob/26ae596b540e92f25da37f978321ba1ec5934bdc/docs/development/phase15-7-recovery.md).
Final publication/CI evidence is appended to that lead repository's current
execution record after both repositories are published.

## Operator behavior

Inspect occupied logins before ordinary login when capacity is full. Public
metadata lists at most three one-way retirement aliases, exact selected host,
interface, device, boot and generation. Aliases cannot address ordinary CR01
traffic and confer no authority. Explicit selection plus the current saved Wi-Fi
password performs a fresh X25519/password proof outside normal login quotas.
One fixed proof workspace expires after five seconds, never becomes a fourth
active session, and retires only the selected login. The selected login may
belong to another operator; membership in a shared password does not prove tab
ownership. A new ordinary login remains explicit. No application request replays.

A native management connection is shared by Pi browser tabs. After browser-to-Pi
loss, read its existing connection state before connecting again. An untouched
native session remains usable. Native-to-Pico uncertainty clears its traffic
keys and retains only bounded nonsecret alias/identity metadata. Explicit
self-retirement invalidates native traffic immediately before its finish proof
can dispatch, including a lost finish reply. Local malformed input and failed
proof starts retain a healthy native session. Another login's retirement leaves
the selected native connection intact. Host busy/ownership/output uncertainty
refuses before recovery transport, under the existing coordinator.

Endpoint edits clear cached retirement inspection and transient credentials.
The confirmation names exact host/carrier and peer impact. Scope/device/boot/
generation and selection identities prevent stale target mutation. A lost
retirement reply is unconfirmed; explicitly repeat a new password proof for the
same nonreused boot-bound alias. An absent result creates no replacement.
Unsupported old firmware keeps supported ordinary reads/saves and reports
recovery unsupported/unconfirmed, without destructive probes or auth downgrade.

Settings/resources attempts retain bounded redacted context. Ambiguity clears
editor revisions and preserves drafts. Read authoritative saved config/resources
under a consistent target/carrier/boot/profile/selection context, then explicitly
review before a new save. Status or reconnect alone cannot unlock saving.
Matching current values prove current state, not attribution of a historical
uncertain save. Saved versus active/null, pending restart and application/storage
fault reporting keep their P15.4 contracts. Credentials remain volatile and scrubbed.

## Validation and review

Genuine native OpenSSL and browser Noble clients exercise production Pico
HTTP/session/AEAD/API with exact clean Pico source. Current tests include frozen
and recovery vectors, public aliases, full active pool, safe absent-target repeat,
wrong password/boot/scope, genuine loaded/armed/running/failed/unknown-output
JobService preservation, and native/runtime continuity. Local-invalid/start
refusals keep native state; pre/post-effect finish loss clears only selected self.
Admission variants preserve the production unsafe-state latch and use isolated
processes rather than inventing an unsafe reset. Earlier fixture teardown
failures remain in scratch logs.

Run in retained Linux (never compile/link Pi C++ on macOS):

```sh
cd src
make wtp-config-test wtp-api-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-network-runtime-test \
  BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= JOBS=2
make wtp-config-interop-test SUDO= \
  PICO_SOURCE="$PICO_SOURCE" MBEDTLS_SOURCE="$MBEDTLS_SOURCE"
make wtp-network-interop-test SUDO= \
  PICO_SOURCE="$PICO_SOURCE" MBEDTLS_SOURCE="$MBEDTLS_SOURCE"
```

Historical runs select `PICO_COMPAT_PROFILE` and its exact named source pin.
Current source-cleanliness guards and historical expectations are retained.
Affected ASan/UBSan runs use leak detection. Browser UI tests exercise connection
state after response loss, explicit confirmation/cancellation, preserved drafts,
fresh review and retirement selection. Actual shipped Pi controls are rendered
at desktop/mobile sizes and light/dark preferences; evidence and final suite/CI
results are in the lead execution packet.

Two independent contexts and executor review found/repaired raw traffic-ID
listing exposure (one-way alias fix), hidden credential entry, stale endpoint
inspection, native self-retirement uncertainty and premature self-invalidation.
The final production proof only clears matching native state at finish dispatch.
No board, RF, GPIO, installed service, physical transport or target heap/stack
qualification is supplied by these deterministic software results.

## Documentation Impact

Updated native/browser API and compatibility documentation, current/P15.6 pins,
CI matrix and this record. Historical P15.1–P15.6 acceptance and exact identities
remain unchanged. Engineering mTLS, USB/raw Plain/TLS WTP/BLE boundaries and the
single runtime authority remain. P15.8+ features are not included.

Operator `/Users/lbussy/GitHub/WsprryPi-Docs` was inspected read-only. P15.14 must
publish consumer carrier/login/recovery instructions, shared-password retirement
impact, old-firmware limits, saved/draft/readback/review flow, commissioning
password/origin continuation and final phone/desktop screenshots. That separate
repository was neither edited nor published.


Acceptance checks passed on the final implementation sources: current pair,
all six exact historical rows, actual published 1090 client against new Pico,
network interop, simulated runtime (34 TLS cases, 358 production checks), affected
ASan/UBSan with leak detection, nine resource combinations and mixed native/
Noble full-capacity recovery. Actual Chrome 155 desktop/mobile light/dark recovery
and readback checks passed, with 8 scoped screenshots and a saved opt-in fixture:

```sh
WSPRRYPI_RUN_RECOVERY_BROWSER=1 node \
  WsprryPi-UI/tests/wtp_recovery_ui_integration_test.js \
  /path/to/current-interop-build /path/to/screenshots
```

Earlier native teardown and Docker fixture-permission failures are retained and
explained in the lead evidence packet; final repaired runs pass. No remaining
actionable production finding was identified in final independent reassessment.
The existing network browser fixture initially stalled CI because it did not
handle the new real saved-state review confirmation. A test-only repair enables
CDP Page events, accepts only the two exact review messages, rejects unexpected
dialogs, and requires at least one actual confirmation. Independent reassessment
found no remaining issue. The original desktop/mobile workflow then passed in
actual Chrome, accepting five expected confirmations with screenshots disabled;
assertions and deadlines were unchanged. Production sources remain those of
`a6e5137a19f50f9b469dafaf18098bdaf400e44e`.

Final clean-commit confirmation and published-head CI are recorded in the lead
repository's [current execution packet](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/phase15-7-recovery.md).
