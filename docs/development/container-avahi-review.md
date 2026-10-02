# Container Avahi contract validation and adversarial review

## Outcome and scope

All four ARMv6/AArch64 Bookworm/Trixie container recipes install
`libavahi-client-dev`. The shared build script rejects missing pkg-config
metadata before compilation and missing direct Avahi client/common NEEDED
entries before successful artifact completion. `elf-dynamic.txt` is retained
and required by the atomic exporter. Generic source builds and installation
behavior are unchanged. No application components or UI sources were modified.

## Validation

- `python3 scripts/tests/container_build_test.py`: PASS, 5 tests, including
  four-target exports and rejection/cleanup when dynamic evidence is absent.
- `python3 scripts/tests/container_avahi_contract_test.py`: PASS, 5 tests,
  including four-target success fixtures, missing development files before
  compilation, either missing direct library despite successful ldd resolution,
  and SONAME-only mentions that must not count as NEEDED entries.
- `python3 scripts/tests/precompiled_binary_test.py`: PASS, 15 tests. The
  printed missing-symbol rejection is expected negative-test output.
- `sh -n containers/build-artifact.sh`: PASS.
- `git diff --check`: PASS.
- `python3 scripts/container_build.py aarch64-trixie
  /private/tmp/wspr-container-avahi-validation-20261002`: PASS, actual Docker
  compile and atomic export. Direct entries for both Avahi libraries and
  resolved runtime dependencies were verified. Exported checksum:

```text
53302b078593962e1d0375493e66fa4fec52e742d811679c91364bd598aac79c  wsprrypi
```

The actual validation artifact comes from the pre-commit working tree and
therefore has dirty build metadata. It is validation evidence, not a published
release artifact. Actual ARMv6 Bookworm/Trixie and AArch64 Bookworm builds were
not repeated in this task; fixture coverage is not evidence of those real
builds. No application execution, host installation, service action, reboot,
Pico operation, live DNS-SD acceptance or RF qualification was performed.

## Adversarial review

First assessment reviewed optional Makefile detection, all four recipe package
lists, direct-versus-transitive linking, exact library names, shell failure
propagation, evidence export/cleanup, CI wiring and documentation scope. It
identified a negative-test gap for non-NEEDED library mentions. Added a
SONAME-only fixture and verified rejection.

Second assessment rechecked the repaired test, fail-before-compile behavior,
each missing direct library, positive four-target fixtures, absent-evidence
atomic export rejection and existing precompiled validation coverage. No
remaining actionable findings in the approved scope. Final focused suites,
shell syntax and diff checks passed; the real AArch64 Trixie build also passed.

## Documentation Impact

Updated `docs/precompiled-installation.md` with the container requirements,
exported evidence and software-only validation boundary. Saved the execution
prompt in `docs/development/container-avahi-execution-prompt.md`.

Reviewed sibling operator documentation read-only: the existing runtime,
transmitter setup and network-interface sections already describe Avahi
advertising and manual endpoint fallback. Operator behavior is unchanged, so
no sibling update is required. Cross-repository writes were not performed.
