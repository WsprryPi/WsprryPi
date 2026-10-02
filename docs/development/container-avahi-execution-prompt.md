# Container DNS-SD build contract execution prompt

Work on WsprryPi `devel`. Inspect status, staged and unstaged diffs and applicable
instructions first; preserve existing work. Fix only the supported precompiled
container build contract. Commit and push the reviewed change to origin/devel.

## Problem and required behavior

The ARMv6 and AArch64 Bookworm/Trixie Dockerfiles omit Avahi development
headers. The Makefile deliberately makes Avahi optional, so a successful
container build can silently omit DNS-SD discovery. Official container artifacts
must retain this functionality; generic source builds retain their optional
Avahi behavior.

1. Add `libavahi-client-dev` to all four pinned container recipes without
   changing base images, architectures, release selection or runtime installation.
2. Fail before compilation if `pkg-config --exists avahi-client` fails, with
   a clear diagnostic. Do not execute the transmitter application.
3. Inspect the produced executable with `readelf --dynamic` and require direct
   NEEDED entries for both `libavahi-client.so.3` and `libavahi-common.so.3`.
   Retain this inspection as exported evidence. Keep existing architecture,
   dependency-resolution, checksum and atomic-export checks.
4. Add meaningful hardware-free tests for absent build dependencies, each
   missing direct library, transitive-only library resolution, successful checks,
   evidence retention and export rejection when evidence is absent.
5. Document the container requirement and its software-only qualification scope.
   Review sibling operator documentation read-only; no cross-repository writes.
6. Run focused Python tests, shell syntax checks, diff checks and a real supported
   container build. Distinguish tests, actual build targets and untested targets.
7. Perform adversarial review, repair findings and reassess until no actionable
   findings remain in scope. Review the staged diff, commit and push; verify
   remote parity independently. Report validation and limitations.

## Boundaries

No application/component refactor, Makefile policy change, UI change, Pi install,
service operation, reboot, Pico action, transmission or RF qualification. This
prompt does not authorize another deployment. Keep generated artifacts outside
the repository. Do not claim all four real builds passed from fixture tests.
