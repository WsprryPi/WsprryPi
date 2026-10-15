# P15.6 native-client compatibility and acceptance

WsprryPico leads three independent consumer sessions. Pi retains one native
management session and its existing operation/management lock, scheduler and
runtime authority. No second coordinator, automatic mutation retry, carrier
fallback or authentication downgrade is introduced.

The current clean Pico dependency is
`792b5b2f60e381a6bd8e7a7726eeffe24fc08519`. Its closed management/2 discovery
advertises exactly three consumer sessions and at most one pending login among
them. Native Pi, phone and Mac can occupy the three slots. Pending login and
rate refusal do not displace healthy sessions. Abandoned slots remain bounded
until logout, five idle minutes or fifteen absolute minutes.

## Compatibility and review

New Pi strictly validates frozen management/1 and new management/2. All fields,
types, operations, identities, carrier selection and availability remain checked;
version 2 has exact integer `consumer_sessions:3` and
`consumer_pending_logins:1`. Unsupported versions and invalid limits refuse.
Capability-reference and discovery-document versions must agree for both the
configuration and shared-resource coordinators. Login preserves distinct
capacity, pending-login and pacing errors; the UI gives finite next actions.

The actual old ConsumerRadioClient translation unit from Pi
`50097e652b4af70d510f4a29d4edf1ffa24d3f0a`, using its unchanged public header,
was compiled against the genuine new Pico crypto fixture. Login returned
`409 unsupported_management_contract`, then logged out only its temporary
session; five exchanges, zero mutations and no fallback were observed. This is
old native client code over the in-process transport boundary, not an installed
old Pi service or physical TCP test. The temporary probe's renamed unused main
produced a return-type warning; no production warning policy was weakened.

Executor reviews checked strict JSON types, capability/document version agreement,
profile/boot identity, finite refusal propagation, local key erasure, native queue
serialization and unchanged mutation/readback policy. They found and repaired a
new test's value-copy warning under -Werror and screenshot capture before resized
layout paint. A later P1 review found version comparison indexed a const JSON
object before checking its closed shape; missing schema could abort. Both
coordinators now validate shape before indexing. Missing schema, array/null
responses and valid-but-mismatched version references are regression cases. No remaining actionable defect was found in the changed Pi software
scope. These are executor reviews; the operator's requested separate post-execution
review remains distinct.

| Profile | Exact Pico SHA | Local result |
| --- | --- | --- |
| legacy | `95aac22bf7cc31b67fe5773761708d743d8a6473` | PASS |
| extended | `dfffdd5220db8b7cf1acf5e74950dbd294283c55` | PASS |
| p152 | `87144d1364f36d04bfe0a714bd1c2f6deb974302` | PASS |
| p153 | `d6208dbc88005383041c5ec306b738db7ec22796` | PASS, including genuine native station/AP crypto |
| p154, old current | `ddb00dd7960373cec271f7816c0e1b10117416e9` | PASS, including genuine native station/AP crypto |
| current | `792b5b2f60e381a6bd8e7a7726eeffe24fc08519` | PASS, including multi-session native/browser and engineering TLS |

Source guards are unchanged and require exact clean history-bearing checkouts.
The five historical sources, pinned Mbed TLS and retained Linux environment were
verified before reuse. Historical fixture expectations were extended explicitly
for p154 rather than repointing every row or silently skipping unsupported peers.

## Validation and reproducibility

All C++ builds execute in hardware-free Linux arm64 image
`wsprrypico-p153-env:local` (`c7f2a891a6ff`), GCC 14.2/CMake 3.31.6/Node 20.19.2,
with no device mounts. Mbed TLS is pinned to
`0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. Nothing was built or linked with
the local macOS Pi C++ toolchain. No SDK, compiler or package was downloaded.

From Pi src, with exact clean Pico and Mbed TLS paths:

```sh
make wtp-config-test wtp-api-test SUDO=
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wtp-network-runtime-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= JOBS=2
make wtp-config-interop-test SUDO= PICO_SOURCE="$PICO_SOURCE" MBEDTLS_SOURCE="$MBEDTLS_SOURCE"
make wtp-network-interop-test SUDO= PICO_SOURCE="$PICO_SOURCE" MBEDTLS_SOURCE="$MBEDTLS_SOURCE"
```

Configuration/API and UI unit checks pass. The simulated production runtime
passes 358 checks. The current pair passes native OpenSSL to Pico/Mbed TLS on
station/AP, strict discovery and coordinator negatives, interleaved traffic from
three native clients, quota refusal, key/context isolation, journal/readback and
all nine UI-produced shared-resource carrier/state cases. Engineering mTLS
loopback includes scheduled 60-second wait, partial I/O, identity failures,
boot changes and same-request job recovery. These are software results.

`make wtp-config-interop-test` also executes the shipped browser bundle in two
independent JS contexts alongside the actual native runtime session, all sharing
one real Pico manager. Three simultaneous sessions, alternating traffic,
capacity refusal, independent browser/native logout, zero WTP I/O and zero
mutations pass. The bundle contains its pinned Noble dependencies; this test adds
no package installation. Production TCP parsing/ACK/FIN/RST and maximum-size
multi-session AEAD are separately exercised by Pico's new HTTP fixture.

Affected ASan/UBSan and leak checks use Debug (-O0), separate output directories,
C++/C instrumentation and matching linker flags. An initial -O1 build stopped on
GCC's maybe-uninitialized diagnostic inside the standard regex implementation;
no compiler warning was suppressed. TLS rotation fixtures run on native Linux
tmpfs because overwriting their owner-only files on the macOS bind mount failed.
Configuration/API, native consumer/coordinator negatives, mixed native/browser
coexistence and all nine resource carrier/state cases pass ASan/UBSan with leak
detection after the final discovery-shape repair. Full commands, retained log
locations, implementation
and publication hashes and required CI identities are recorded in the
[companion Pico handoff packet](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/phase15-6-sessions.md).

## Documentation Impact

Updated: this acceptance/compatibility record, management API session guidance,
source-pair matrix, exact source pins, historical CI row and finite login feedback.
Considered unchanged: WTP/1, Config/update and resource schemas, recurrence,
ownership, protected host routing, uncertain-write readback and operation locks.

Impeccable was applied to the scoped feedback. Unit and actual Chrome checks
exercise desktop/mobile light/dark, preserved drafts, failed saves, independent
connection state and capacity/pending/rate wording. A capture-timing repair keeps
feedback visible after resizing; the second bounded pass confirms it. No product
layout or theme redesign was made.

P15.7 remains open. A lost reply may retain a bounded abandoned server session;
no unauthenticated clear-all recovery was added. Physical phone/Mac concurrency,
installed services, target heap fragmentation/stack high-water, timing and RF are
unqualified. Shared-password trust, HTTP page substitution and active fake-endpoint
offline guessing remain the existing limitations.

P15.14 must update the operator docs for management/1 versus /2 compatibility,
three total slots, native Pi occupying one, one pending login, finite pacing,
independent logout/expiry, abandoned tabs, preserved edits and authoritative
readback. Replace materially inaccurate desktop/mobile light/dark screenshots
after P15.7 and physical workflows settle. The separate operator documentation
repository was not modified or published.

The concrete operator-doc follow-ups are
`docs/User_Interface/Setup/Transmitter/index.md#pico-development-controls` for
connection/session/capacity/conflict/readback workflows and screenshots, and
`docs/Advanced_Operations/ini_configuration/transmitter_backends.md#pico-wtp`
for the separate WTP transport and management-carrier boundaries. Both paths
were inspected read-only in WsprryPi-Docs; no docs build or publication occurred.
