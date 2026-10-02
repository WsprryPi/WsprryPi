# Execute: isolate Si5351 inventory from GPIO configuration

## Objective and scope

Work directly on WsprryPi `devel`. Add both discovery findings as TODOs; complete
Si5351 inventory isolation first and leave reply-routing work pending. Preserve
existing source, configuration, endpoint identity, Fleet assignments and RF state.
The reproduced failure is wspr4's GPIO4 configuration/discovery GET synchronously
scanning Si5351 while holding the shared configuration mutex.

## Required implementation

1. Trace defaults, INI/JSON parsing, validation, serialization, browser population,
   persistence, scheduling and runtime readiness. Public configuration serialization
   must read only inventory metadata/cache and never perform an I2C transaction,
   regardless of selected backend. An unchecked inventory must remain unconfirmed.
2. Keep the existing explicit inventory endpoint and fresh server-side validation
   of bus/address selections. Isolate physical scans in an executable subprocess
   entered before singleton acquisition, services, GPIO or transmitter setup.
   Use argument vectors, bounded result size, a fixed production deadline and
   nonblocking cleanup. A stalled kernel transaction must not force a blocking
   wait or accumulate unbounded workers. Close inherited application descriptors.
3. Do not hold the shared configuration lock across selection/readiness probes.
   Recheck configuration revision before committing a candidate; a competing
   update must cause conflict rather than overwrite. Do not trust client Platform
   inventory or cached data as permission to use a transmitter.
4. Keep the scan restricted to 0x60 through 0x6F and non-configuring register reads.
   Do not change the reusable Si5351 transmitter component, physical capabilities,
   safety gates, schedules, output settings or backend fallback behavior.
5. On GPIO, browser configuration loading must preserve inactive Si5351 settings
   without scanning I2C. On explicit Si5351 selection/loading, refresh inventory
   separately; distinguish unchecked, empty, busy, failed and timed-out states;
   retain drafts and guard against late replies after a backend/bus change.

## Validation and acceptance

- Do not attempt any local Mac C++ compilation/linking. Build C++ on wspr5 Linux
  in an isolated source directory, transferring source through compressed streams.
- Add meaningful subprocess tests for success, malformed/oversized output, failed
  execution, timeout, concurrent requests, cleanup and recovery. Use typed test
  seams; no production fault-injection environment or UI controls.
- Add regression assertions that public snapshots perform zero physical probes,
  GPIO config/discovery remain responsive during stalled refresh, cache keys do
  not cross buses/references, and concurrent updates preserve newer revisions.
  Exercise both initial logging states in the INI revision test on every host.
- Audit the strict GPIO-free Si5351 profile: an unavailable bus must be rejected
  before any adapter open; separately audit the isolated worker against a verified
  nonexistent selected I2C path. Preserve checks against GPIO, mailbox, MMIO, RP1,
  other adapter paths and I2C ioctls.
- Run focused inventory/configuration tests and the default hardware-disabled
  Linux semantics profile. Run UI unit/browser regressions and inspect affected
  desktop/mobile states using Impeccable. Record exact commands and limitations.
- Validate the repaired application on wspr4 GPIO4 with local Enable off and no
  WTP jobs: repeated config/discovery reads, explicit scan, parallel reads and a
  reversible unrelated config write. Preserve INI and Fleet data and end inactive.
  Do not transmit; this task qualifies management responsiveness, not RF.
- Review adversarially against blocking I/O, child lifetime, inherited sockets,
  stale cache/readiness, revision races, forged inventory, browser auto-probing,
  stale browser replies and dishonest detection state. Repair every finding in
  this scope and run another adversarial assessment after repair.

## Documentation and delivery

Update the development report/TODO and `docs/i2c-bus-selection.md`. Review sibling
operator documentation read-only and report any separately required change.
Save execution results and both review passes in the repository. Keep historical
DNS-SD observations intact and separate them from this repair's evidence.
Review the complete staged diff, commit only this task on `devel`, push
`origin/devel`, independently verify remote parity, and report changes, tests,
live evidence, outstanding routing TODO, documentation impact and tree status.
