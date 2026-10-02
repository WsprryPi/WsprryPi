# wspr5 inbound WTP on RP1 GPIO20

2026-10-01, `devel`. **Passed: wspr4 remotely submitted five finite WTP jobs to
the installed wspr5 service on GPIO20. TONE, QRSS, FSKCW, DFCW and one full WSPR
frame completed, with corresponding SDR signals, output off and ownership
released.**

The earlier Fleet campaign used wspr5 as the central controller's local output.
It omitted physical acceptance of wspr5's inbound RP1 WTP adapter. The GPIO20
connection was already available; no cabling limitation prevented this test.
That omission is now closed. Eight-way testing is excluded by the operator's
instruction. The agreed Fleet campaign uses five remote members plus central
local work and two receiver observation groups.

## Execution and evidence

| Mode | RF base | Intended duration | Result |
| --- | --- | --- | --- |
| TONE | 14.097100 MHz | 10 seconds | Complete; corresponding SDR signal; off and released. |
| QRSS | 14.097100 MHz | 5-second keyed fixture | Complete; signal and keyed gaps observed; off and released. |
| FSKCW | 14.097100 MHz, 4 Hz shift | 5-second fixture | Complete; corresponding signal; off and released. |
| DFCW | 14.097100 MHz, 4 Hz shift | 5-second keyed fixture | Complete; signal and keyed gaps observed; off and released. |
| WSPR | 14.097100 MHz, normal tone spacing | 110.592 seconds, 162 canonical symbols | AA0NT / EM18 / 20 dBm message metadata; complete with corresponding full-frame signal; off and released. |

Starts were 16:49:52, 16:50:27, 16:50:57, 16:51:27 and 16:51:57 Central
Daylight Time, respectively. Each job had a distinct ID and fresh service boot.
The client socket originated on wspr4 (`192.168.1.120`) and addressed wspr5
(`192.168.1.54:31417`), device `e045017b11e54c2fb7029f0154e6deb9`.
This tests the physical inbound adapter through a remote WTP client; it does not
add a sixth remote assignment to the existing central Fleet batch.

The existing RP1 development profile requires launch confirmation for the exact
job ID and GPIO route. Each managed launch carried its own finite confirmation,
verified against actual process arguments before submission. Provider integrity,
frequency policy and local priority were preserved. The approved frequency
override remained unchanged. This result applies to GPIO20 with that existing
authorization profile; it does not claim a new RP1 authorization workflow.

- [Acceptance](wtp-rp1-inbound-results/acceptance.json): all five modes passed;
  minimum on-signal peak above nearby median noise ranged from 86.65 to
  88.16 dB. All five receiver captures had zero overflows, timeouts and clipped
  samples, with verified device/stream cleanup.
- Per-mode `*-summary.json` records retain HELLO/CAPS/clock, LOAD frequency
  realization adjustments, ARM, state transitions, terminal completion and
  RELEASE. WSPR reused the previously generated canonical frame in
  [frame.json](wtp-rp1-inbound-results/frame.json). The other modes are finite
  RF-event execution fixtures.
- [Final six endpoints](wtp-rp1-inbound-results/final-six.json) independently
  reported empty, unowned and output inactive after restoring the service.
  [Controller state](wtp-rp1-inbound-results/final-controller.json) confirms
  local Enable off, all five assignments paused and no unknown output.
- [Restoration](wtp-rp1-inbound-results/restoration.txt) verifies the canonical
  `/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini` process, unchanged
  installed binary/helper and unchanged INI/assignment hashes. The temporary
  `99-rp1-inbound-acceptance.conf` was removed. GPS/PPS and receiver calibration
  were unchanged; the current combiner connections can remain.

Installed binary SHA-256 remains
`5c1abc72cc8aa67e4a5a2630fafea2329675d3d71189e00c11b6ddff260f8554`.
Application source is unchanged from `9a1b8c1`; its reviewed
[deployment manifest](wtp-managed-rp1-fleet-results/source-manifest.json) binds
the pre-commit version label to that source. No binary installation was performed
in this test.

Large raw recordings and complete wire observations remain outside Git on wspr5
at `/home/pi/wtp-rp1-inbound-20261001`. Their hashes and sizes are retained in
[the private evidence manifest](wtp-rp1-inbound-results/private-evidence-manifest.json).
The original client observations also remain in the same stage on wspr4.
Transfers used tar piped through gzip. Git contains compact observations and
capture metadata, rather than an RF-chain inventory.

SDR acceptance is the operator-selected criterion: a corresponding signal.
Analysis uses 65,536-sample FFT windows every 0.25 seconds, checks event interiors
and post-job absence, and observes keyed gaps. It does not measure calibrated RF
edge timing, decode success, spectral purity or frequency accuracy.

## Validation and review

Executed from the Mac:

```sh
python3 -u docs/development/wtp-rp1-inbound-results/run.py /home/pi/wtp-rp1-inbound-20261001 docs/development/wtp-rp1-inbound-results
python3 docs/development/wtp-rp1-inbound-results/verify.py
python3 -m py_compile docs/development/wtp-rp1-inbound-results/client.py docs/development/wtp-rp1-inbound-results/run.py docs/development/wtp-rp1-inbound-results/analyze.py docs/development/wtp-rp1-inbound-results/verify.py
```

The runner invoked the retained client on wspr4, five bounded SDR captures on
wspr5 and exact-job managed launches. SDR analysis ran with NumPy on wspr5:

```sh
python3 /home/pi/wtp-rp1-inbound-20261001/analyze.py /home/pi/wtp-rp1-inbound-20261001
python3 /home/pi/wtp-managed-rp1-fix/source/docs/development/wtp-managed-rp1-fleet-results/final-state-probe.py /home/pi/wtp-rp1-inbound-20261001/final-six.json
```

These checks passed. [Adversarial review and reassessment](wtp-rp1-inbound-results/adversarial-review.md)
checked distinct job/boot identities, actual remote sockets, exact confirmation,
complete finite timelines, corresponding RF, receiver cleanup and restoration.
No application defect was found. Python/JSON checks, evidence assertions and
final whitespace checks passed. Application, component and UI source are
unchanged, so source suites and Impeccable rendering were not repeated.

## Current remaining testing

| Area | Remaining agreed work |
| --- | --- |
| Live Fleet browser | Create/edit assignments through the actual browser. Resume/Pause/Remove and both local takeover choices already passed. |
| Discovery | Link loss, address changes, multihomed recovery and confirmed residual cache expiry using wspr5's Ethernet/WiFi interfaces. |
| CI | On base `3dd0a79`, Mac, full Linux, strict I²C and GCC 13 release passed. Network/browser failed at “Render network controls with mocked responses.” [Refreshed snapshot](wtp-fleet-reconnection-results/ci-status.json). |

[Physical Fleet reconnection](wtp-fleet-reconnection-report.md) subsequently
passed the agreed five-target outage/crash/restart campaign after repairing
changed-boot reconciliation. Actual browser Reconcile also passed. This closes
Fleet recurrence; it does not qualify the separate Discovery topology changes.

Installed-service, clean-card installation, GPIO4 live recovery/power failure,
physical takeover/INI/HTTP cancellation, mixed physical Fleet concurrency, both
receiver groups and wspr5 inbound GPIO20 acceptance have recorded passes.
Eight-way testing is excluded and is not an outstanding acceptance gate.

## Documentation Impact

- Updated: this physical result, compact evidence, historical-report status
  links and the reconciled development testing backlog.
- Considered unchanged: `docs/wtp-fleet.md`, `docs/wtp-pi-operation.md` and the
  sibling operator backend/Fleet/local Enable documentation. Existing backend
  capabilities and operator behavior have not changed.
- No operator-documentation update is required for this evidence-only test.
  No sibling repository, UI or reusable component source was modified.
