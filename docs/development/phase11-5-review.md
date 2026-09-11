# Phase 11.5 target resource and contention review

Status: **OPEN**. WsprryPico coordinates the joint plan, case register and target
measurement definitions in its `docs/development/phase11-5-*.md` and
`phase11-5-register.json`. No physical configuration is accepted yet.
Phase 11.4 remains closed within its bounded inhibited matrix and eight-hour soak.

## Scope and source identity

The user confirmed this division on September 11, 2026:

- 11.5 accepts resources and contention for each PIO clock selected for 11.6,
  binding memory, both stacks, refill/launch and contention to exact firmware
  and clock, with deadlines recalculated for every tested clock.
- 11.6 performs per-band/per-mode conducted acceptance at those selected clocks.
  Alternatives are investigated only for failure or unresolved selection;
  selecting another clock repeats the affected 11.5 checks before acceptance.
- 13 owns the systematic band x mode x clock comparison, final supported
  configurations, filters, spectral qualification and release firmware.

The selected candidate is 138 MHz, system clock equal to PIO sample clock,
PIO divider 1. The accepted list is empty. Physical 132 and 150 MHz checks are
untested; the earlier inhibited 150 MHz soak does not qualify either physical
worker timing or RF. This work does not perform a comprehensive RF clock sweep.

The Mac checkout began clean on devel at
`89f23e5d10c8a46ead9f37c7cefa8867280ac4df`. After direct user authorization it
fast-forwarded to `a4eb591813b19ece8bba30f6ca072066670c7d1d`. The incoming scheduler
fix `923ab570fe53ef2ccca7d12e519c9dc36adf7e93` sends bounded STATUS keepalives
while awaiting a future start. It belongs in the campaign's production baseline.
The wspr5 source also reported a4eb591; its independently hashed installed
executable differs from that source identity and has not been replaced.

Pi owns production integration, load generation and host evidence. Pico owns
firmware, WTP and the target measurements. No protocol, transport fallback,
identity bypass, global trust change or UI redesign is introduced here.

## Findings and changes

The runtime test consumed `tls-fixtures` without depending on their generating
TLS target. The initial parallel run failed on a missing runtime-rotation
certificate; serial TLS then runtime tests passed. `wtp-network-runtime-test`
now depends on `wtp-tls-test`. The repaired parallel invocation passed, including
2,012 production configuration/runtime checks. This is a fixture dependency
repair, not a change to production scheduling or recovery.

Current network guidance incorrectly described the full WTP device ID as the
normal hostname and MAC suffixes as optional aliases. `docs/wtp-network.md` now
uses the accepted last-six-station-MAC default, retains independent full-device
validation and certified custom names, and states the DHCP plus mDNS workflow.
No static-address requirement is introduced.

Pico's review found an expensive core-1 stack scan on every RPC, overlapping
service-gap/poll interpretations, incomplete heap/fragmentation observations and
missing matched refill evidence. Its scoped instrumentation and evidence checker
address parts of those findings; physical memory, stack, launch/tail, observer
cost and contention gates remain open. SDK allocator panic-on-exhaustion remains
an explicit unresolved acceptance limitation, not a claimed graceful rejection.

## Validation and evidence boundaries

Hardware-free checks run on the Mac from `src`:

- `make -j4 wtp-production-test wtp-network-runtime-test wtp-api-test
  wtp-network-process-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
  WTP_NETWORK_BUILD_DIR=build/phase11-5-network`: PASS after dependency repair.
  This includes TLS tests, generated ephemeral fixtures, API and process checks.
- `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j1 semantics-test-portable SUDO=`:
  PASS for the explicitly simulated portable subset; it is not the Linux full
  physical-backend semantics profile.
- `make -j1 wtp-network-interop-test SUDO= WTP_NETWORK_BUILD_DIR=build/phase11-5-network`
  with explicit Pico and Mbed TLS source paths: PASS against clean Pico
  `ce1c339a976e795e90c38c4a57578f9c8ed75615` (repeated after the initial
  ab87031 pass) and Mbed TLS
  `0bebf8b8c7f07abe3571ded48a11aa907a1ffb20`. The build uses Pico's hash-checked
  alert-delivery overlay, not an unpatched upstream TLS approximation. Named
  and direct-IP identities, shared management, finite jobs, lost-response replay,
  ownership, boot/device mismatch and same-session recovery passed, including
  the actual 60-second future scheduled wait. macOS could not bind the second
  loopback IPv4 address; that actual rebind subcase was explicitly skipped while
  injected-address checks ran. It remains a native-Linux/physical fixture gate.
- Reciprocal Pico-owned actual-client and network interop: both PASS against
  clean Pi `76fd1018868551b93ca46bef2e7c2ef28fb75993` (85.98 seconds).
  The later companion changes update only this evidence and the Pico source pin;
  the production client and scheduler sources are unchanged.

Logs and original failed attempts remain in the coordinating checkout's ignored
`build/phase11-5`. Injected transport failures and host device-tree diagnostics
are part of the portable tests, not observations of a transmitting Pico.
No installed service, binary, INI, network interface or Pico firmware was changed.

Authorized P0 readbacks on wspr5 verified both exact Pico serials as inhibited,
empty, unowned, output false; identities and boots are recorded in the joint
plan. User-confirmed wiring is 60 dB fixed attenuation per combiner input, 50 ohm
attenuator loads and no filters. A separate user-requested GPSDO status read
reported both outputs already enabled at 10 MHz LOW, with satellite/PLL lock;
no output setting was changed. These observations are not an RF campaign.

## Documentation Impact

Updated: this companion review and current network configuration guidance;
the coordinating repository holds the plan, metric definitions, clock register
and historical 11.4 supersession pointers.

Considered and unchanged: normative WTP, shared browser API, architecture and UI.
No interface code changed, so no Impeccable or UI visual workflow was required.

Still required after measured acceptance, in the separate read-only
Wsprry_Pi_Docs repository:
`docs/Advanced_Operations/ini_configuration/transmitter_backends.md`,
`docs/Command_Line_Operations/transmitter_backends.md`,
`docs/User_Interface/Setup/Transmitter/index.md`,
`docs/User_Interface/Operations/index.md`, `docs/Advanced_Operations/rest_api.md`
and `docs/User_Interface/Maintenance/network_safety.md`.
Only measured supported combinations should be published there.
