# macOS Fleet CI repair

## Scope and outcome

The repair follows [the execution prompt](wtp-macos-fleet-ci-prompt.md) on
`devel`, starting at `ad1840b4a42c861c93ccd6d627ea697547c09b2a`.
It changes only the Fleet regression test and development documentation.
Production code, JSON schema, runtime guards and CI coverage are unchanged.

The original failure was in the
[macOS job of run 36642658611](https://github.com/WsprryPi/WsprryPi/actions/runs/36642658611/job/109658298885):
`wtp-fleet-test` aborted with `Invalid fleet assignment document`. That job had
already passed portable semantics; its following container-export test step
was skipped. The run's Linux non-hardware suite, WTP network loopback,
GCC 13 release profiles and strict I2C job passed.

The Mac does not need a transmitter for this test. It validates Fleet documents,
schedules, revocation, ownership and worker lifecycle with temporary files and
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`.

## Root cause and repair

The crash-recovery fixture assigned a single JSON object using:

```cpp
d["assignments"] = {b};
```

Clang 18.1.8 made this an object, while local Apple Clang 21 made it a
one-element array. The store requires an array and correctly rejected the
older compiler's result before the intended worker test could start. A debugger
backtrace located the exception in `WtpFleetStore::update` called from that
fixture. A minimal program independently reproduced the different JSON shapes;
Apple Clang 21 still produced an array when using the same libc++ 18 headers.

The test now requests the intended type explicitly:

```cpp
d["assignments"] = Json::array({b});
```

The two-output fixture uses the same explicit construction. Additional checks
verify that the crash fixture contains exactly one complete assignment, and
that assigning an object instead of an array is rejected without changing the
in-memory document, its revision, or the document read by a fresh store.

## Local evidence

The host was ARM64 macOS 27 with Apple Clang 21. An isolated official Homebrew
Clang 18.1.8 toolchain was extracted under `/private/tmp`; no installed compiler
or system configuration was changed. It provides a reproducer for the older
compiler behavior, not a replacement for checking the actual GitHub runner.
The original runner was macOS 15 with Xcode 16.4 as its default toolchain.

| Check | Result |
| --- | --- |
| Original Fleet fixture, current Apple Clang 21 | Passed; did not reproduce CI failure |
| Fresh Apple Clang 21 objects, deployment target 15; 30 executions | Passed; no intermittent failure found |
| Original Fleet fixture, fresh Clang 18.1.8 objects | Failed with the exact `Invalid fleet assignment document` exception |
| Minimal single-object JSON assignment, Clang 18.1.8 vs Apple Clang 21 | Object vs array; explicit `Json::array({b})` produced an array on both |
| Repaired complete Fleet test, Clang 18.1.8 | Passed |
| Repaired complete Fleet test, Apple Clang 21 | Passed |
| Container-export regression tests | All five passed |
| Final whitespace/error check | Passed |

Focused commands, run from `src`:

```sh
SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk \
MACOSX_DEPLOYMENT_TARGET=26.5 \
make wtp-fleet-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  CXX='c++ -Wno-deprecated-declarations'

MACOSX_DEPLOYMENT_TARGET=15.0 \
make wtp-fleet-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO= \
  CXX='/private/tmp/wtp-llvm18/llvm@18/18.1.8/bin/clang++ -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk -Wl,-syslibroot,/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk -Wno-unused-command-line-argument -Wno-deprecated-declarations' \
  OBJ_DIR_DEBUG=build/obj/macos-fleet-clang18 \
  BIN_DIR=build/bin/macos-fleet-clang18

python3 ../scripts/tests/container_build_test.py
```

The SDK override accommodates the local installed linker. Linking against local
OpenSSL 4 emitted warnings because those libraries target macOS 27 and the
commands use older deployment targets; execution was on macOS 27. These runs
do not establish compatibility of those local libraries with older macOS.
No such workaround or toolchain override is added to CI.

## Adversarial review and reassessment

The review traced schema validation, copy-before-validation, persistence,
fresh-store reload, and worker startup/shutdown. It checked that:

- The repair cannot pass by skipping the crash fixture or starting no worker.
  Existing API and worker-state checks still run, followed by the persisted
  `in_flight` assertion after shutdown.
- The malformed-object case uses a valid revision and otherwise valid
  assignment. Rejection is followed by memory, revision and disk checks.
- Zero-output, two-output, revocation-to-one-output and explicit single-output
  documents remain covered.
- Production validation and transport guards remain intact. No test exception
  is swallowed to make the failing fixture pass.
- No other implicit `assignments = { ... }` construction remains in the C++
  source, and no reusable component, UI or workflow was modified.

The final reassessment of the diff and before/after results found no remaining
material finding within this repair's scope. Broader local semantics were not
repeated because shared production behavior is unchanged. The existing workflow
reruns both macOS portable and Linux full semantics for the pushed commit.
Its exact commit and run result belong in the delivery report; the local results
above alone do not establish a passing GitHub run.

## Documentation Impact and boundaries

- Updated: this development report and the comprehensive execution prompt.
- Considered but unchanged: `docs/wtp-fleet.md` and the sibling operator
  documentation's `docs/User_Interface/Setup/index.md`. Fleet behavior and its
  saved-state contract have not changed, so no operator instructions or
  screenshots require revision.
- Still required for this repair: no operator-documentation changes.
- UI/Impeccable review: not applicable; no interface change.
- Hardware, RF, Pi services and installation: not exercised or qualified here.
  The separate live Si5351 WSPR readiness issue is outside this CI repair.
