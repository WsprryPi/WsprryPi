# Issue 446 execution prompt

Implement https://github.com/WsprryPi/WsprryPi/issues/446 in
`/Users/lbussy/GitHub/WsprryPi`, on `codex/fix-446-legacy-tone-frequency`,
starting from `654d51ab253fb5aaa26cf0ef3e8208cd2ffcfd6f`.

## Review findings

The initial working tree was clean. The recent parent commits record WTP/Pico
campaign evidence. Relevant implementation history includes `04507c0` (RP1
TONE controller dispatch), `a62e466` (legacy clock identity), `b2b6a40` and
`271a463` (shared/intrinsic chipset corrections), and `72f882a` (WTP lifecycle).
These separate backend dispatch, frequency planning, and parent-clock correction.

The legacy TONE route remains outside the execution-plan controller. It builds
a WSPR-spaced `WsprTransmissionPlan` from the committed RF request and emits
symbol zero continuously. That subtracts 1.5 spacings, or 2.197265625 Hz. The
same plan builder supplies setup and emission. Configuration can replace the
committed RF with a hardware-adjusted center; restart reuses the committed
request. A center-only compensation risks confusing the carrier with that
internal center or applying the adjustment twice.

The GPIO table also shifts its center when divider integer parts differ.
Test the actual table arithmetic, including this boundary, rather than only
the shared compiler's already-correct TONE event. Si5351 uses event RF directly,
RP1 compensates its table reference, and WTP serializes event frequency.
WSPR's four centered tones and existing QRSS/FSKCW/DFCW adaptation are correct
and must remain unchanged.

## Authorized scope and design

Make the smallest maintainable fix inside `src/WSPR-Transmitter`, with focused
parent test integration and developer documentation. This is a component-owned
frequency defect: a parent/UI workaround would miss standalone callers and
risk corrupting other backends or dial/audio conversion.

Represent legacy TONE as a zero-spacing single-frequency table at the final
requested RF. Keep setup, divider-table construction, emission, and repeat/
recovery reconstruction on that same plan. Retain non-TONE spacing semantics.
Extract only the pure arithmetic needed to exercise the production table and
emitter contract without MMIO/DMA. Validate representability and reject an
exact TONE if the existing safe-divider constraints would silently move it.
Keep normal modulated-mode hardware-limit behavior unchanged.

Do not change clock constants, calibration composition/sign, other backends,
frequency catalogs, scheduling, interlocks, selectors, persistence, or UI.
Avoid new CLI/environment controls and test hooks in production workflows.
No transmissions, GPIO operations, installation, service changes, reboot, or
live-device qualification are authorized. `wspr4` may be used for compilation
and hardware-disabled tests; preserve its existing checkout and installed
software, using an isolated build directory if needed.

## Implementation and validation

1. Follow root and any nested repository instructions. Inspect both staged and
   unstaged changes and preserve unrelated work. Read the simulated-backend
   contract before any application-level hardware-free test.
2. Add regression coverage that fails against the old TONE mapping and reaches
   production divider-table and emitter arithmetic. Independently assert the
   intended carrier, valid divider brackets, integer-boundary rejection, and
   unchanged WSPR frequencies. Exercise all supported legacy processor/parent
   profiles, low/high representative frequencies, nonzero correction, and
   repeated plan reconstruction without accumulating any offset.
3. Preserve QRSS/FSKCW/DFCW frequency mapping and check nonlegacy TONE plans.
   Review CLI, scheduler/WebSocket, and standalone TONE call paths to verify
   final-RF requests are neither shifted nor persisted upstream.
4. Use existing Makefile test infrastructure. Run the focused component suite
   and applicable parent regressions; on macOS explicitly run
   `make semantics-test-portable SUDO=` from `src`. Inspect every target before
   running it. Run full Linux semantics only in a suitable Linux environment
   with `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1` and `SUDO=`. Record commands,
   outcomes, skips, and limitations. Do not present simulation as physical RF
   evidence. Run final whitespace checks.
5. Review operator documentation in `../Wsprry_Pi_Docs` read-only, especially
   `docs/User_Interface/Maintenance/index.md`,
   `docs/Command_Line_Operations/service_test_controls.md`, and
   `docs/Advanced_Operations/timing_calibration.md`. Save a developer note in
   this repository explaining corrected/historical behavior, divider limits,
   calibration interpretation, validation, and the exact documentation follow-up.
   Separate-repository documentation edits are not authorized. No UI changes
   are planned; any necessary UI scope would require the mandatory Impeccable
   workflow before proceeding.
6. Perform adversarial review after implementation: independently challenge
   boundary handling, same-integer divider requirements, source selection,
   finite precision, calibration, carrier/status consistency, reconfiguration,
   failure cleanup, build profiles, and regression-test sensitivity. Repair
   findings, rerun affected checks, and repeat the assessment until no
   actionable findings remain in the authorized source scope. Record findings
   and closure evidence; keep physical qualification explicitly outstanding.
7. Review the complete staged component and parent diff, commit the authorized
   change, push this branch to `origin`, verify remote parity, and report the
   implementation, tests, adversarial assessments, documentation impact,
   remaining qualification, branch, commit, and working-tree state. Do not
   merge, create a PR, close #446, or claim RF/release qualification.

## Acceptance boundary

The source fix must make legacy TONE's nominal carrier equal the committed RF
or fail explicitly before output when safe exact synthesis is unavailable.
All other modes retain their established frequency contracts. Historical
affected-build measurements retain raw evidence and the old -2.197265625 Hz
interpretation; corrected builds must not receive that adjustment again.
Software acceptance and separately authorized conducted RF acceptance remain
distinct. The final report must name any uncompleted external gates.
