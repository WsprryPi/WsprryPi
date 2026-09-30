# Execute the macOS Fleet CI repair on devel

## Objective and evidence

Diagnose and fix `wtp-fleet-test` failing with an uncaught
`Invalid fleet assignment document` exception in GitHub Actions run
36642658611, macOS job 109658298885, for commit
ad1840b4a42c861c93ccd6d627ea697547c09b2a. Work on `devel`, preserve unrelated
changes, and use the existing build/test infrastructure. Establish the exact
failure mechanism before changing production behavior.

The Mac is a software test host. Fleet fixtures use temporary documents and
fictional targets. Keep `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`, explicit simulated
build selection and the existing transport guards. No transmitter, Pi service,
installation, GPIO, I2C or RF operation is needed for this repair.

## Investigation and implementation

1. Inspect current branch, staged/unstaged state, applicable instructions, CI
   steps, exact failure log, Fleet test, document store, runtime and build rules.
2. Trace fixture creation, validation, persistence, reload, revocation, worker
   startup/shutdown and error handling. Check toolchain/platform differences,
   stale builds and nondeterminism against evidence rather than assumptions.
3. Reproduce the failure or construct a deterministic regression for its proven
   cause. Preserve failed-run evidence and distinguish local from CI results.
4. Apply the smallest maintainable repair at the responsible boundary. Do not
   disable the macOS job, skip the failing check, loosen the document schema,
   catch and ignore unexpected failures, or add hardware fallback behavior.
5. Add meaningful coverage that fails before the fix and passes afterward,
   covering valid documents, malformed input and persistence/restart semantics
   where affected. Keep component boundaries intact; do not refactor unrelated
   code or change operator workflows.

## Validation, review and delivery

Run the focused Fleet target with the explicit macOS simulated profile. Run
relevant adjacent regressions and the portable semantics suite if the repair
changes shared behavior. Verify Linux coverage through the existing CI workflow.
Use bounded repeated tests only if needed to reproduce or resolve a race.

Perform an adversarial review of the fix and tests, including alternate failure
paths and whether the tests can pass without checking the intended contract.
Repair material findings, rerun affected tests and perform another assessment.
Record the root cause, fix, exact commands/results, review findings and closure,
remaining limitations, and documentation impact in a durable development report.
Review the corresponding operator documentation; leave it unchanged when this
is solely an internal correctness/test repair with no operator contract change.
Use Impeccable if UI changes actually become necessary.

Review the complete staged diff, commit and push the authorized `devel` branch.
Verify remote parity and inspect the new CI run for the pushed commit. If CI
finds a related issue, repair, review, retest and push a follow-up. Report success
only after the macOS failure is closed with evidence; distinguish any unrelated
remaining CI or physical-runtime issue. Do not alter the separate live Si5351
WSPR readiness investigation as part of this task.
