# Concurrent Fleet: second receiver group

2026-10-01, `devel`, tested controller source
`9a1b8c10036f77f8446b912c481b3ea5dc3d39e8`.

**Passed: the managed wspr5 local WSPR frame ran alongside five physical remote
targets. All fifteen intended remote jobs completed, and the SDR observed the
three newly connected outputs. The earlier managed RP1 authorization failure
is closed for this bounded concurrency test.**

This completes the physical acceptance left open by
[the managed RP1/Fleet fix prompt](wtp-managed-rp1-fleet-fix-prompt.md) and
[its implementation report](wtp-managed-rp1-fleet-fix-report.md).

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Five physical remote targets | Passed | wspr1, wspr2, wspr4, Pico A and Pico B each completed three ten-second TONE jobs. Fifteen distinct job IDs; successful execution and cleanup, terminal output inactive. |
| Simultaneous remote transmission | Passed | Twelve observations of all five outputs active in each of the three common UTC slots. |
| Independent central local scheduling | Passed | Managed wspr5 RP1 GPIO20 completed one normal WSPR frame while all five remote outputs transmitted in the first slot. Read-only local transmitter status independently reported `transmitting`, then `complete`. |
| Corresponding SDR signals | Passed | wspr2 GPIO4 and Pico A GP2 observed in every remote slot; wspr5 GPIO20 WSPR observed throughout the local frame interior. Clear on/off contrast for all three. |
| Receiver lifecycle | Passed | Complete capture, zero overflows/timeouts/clipped samples, successful stream deactivation, closure and device release. |
| Cleanup and service restoration | Passed | Five assignments paused, local Enable off, no in-flight work or unknown output. Independent HELLO/STATUS on six distinct endpoints: empty, unowned, inactive. Temporary confirmation removed; canonical managed service active. |

The operator confirmed the stopped cable rotation before this run. The SDR
remained on wspr5; the three device taps were wspr2 GPIO4, Pico A GP2 and
wspr5 GPIO20. The other targets remained on attenuator/snubber connections.
The earlier group, wspr1/wspr4/Pico B, has corresponding SDR evidence in
[the historical first-group report](wtp-concurrent-fleet-report.md).
All outputs in this campaign use GPIO/PIO; no Si5351 is involved.

## Executed batch

The five saved assignments retained their original per-output definitions:

| Output | Mode | Frequency | Duration |
| --- | --- | --- | --- |
| wspr4 GPIO4 | TONE | 14.098100 MHz | 10 seconds |
| wspr1 GPIO4 | TONE | 14.099100 MHz | 10 seconds |
| Pico A GP2 | TONE | 14.100100 MHz | 10 seconds |
| wspr2 GPIO4 | TONE | 14.101100 MHz | 10 seconds |
| Pico B GP2 | TONE | 14.102100 MHz | 10 seconds |
| wspr5 GPIO20 | WSPR, AA0NT / EM18 | 14.097100 MHz RF base | One intended 110.592-second frame |

Remote starts were **20:56:01, 20:58:01 and 21:00:01 UTC**. The local frame
started in the first window. The native completion log reports
**110.725809 seconds** of execution elapsed time. That completion value includes
execution/polling overhead; this record does not equate it with a calibrated RF
edge-duration measurement. The compiled frame's intended duration is
110.592 seconds. Half-second FFT sampling found the local signal across a
110.5-second span of sample centres, consistent with the intended frame.

The bounded observer disabled local output after completion and paused remote
assignments after the third round. The scheduler had already prepared a fourth
future slot at 21:02:01 UTC; those five pending jobs were canceled before their
RF window. Their consumed slots were preserved. They are separate from the
fifteen successful intended jobs, and no consumed job was replayed.

## Evidence and checks

- [Controller analysis](wtp-concurrent-fleet-part2-results/controller-analysis.json):
  per-device completion records, distinct jobs, simultaneous output observations
  and the native local completion log.
- [SDR analysis](wtp-concurrent-fleet-part2-results/rf-analysis.json):
  corresponding signals and post-job contrast. Local WSPR minimum peak over
  masked nearby median noise was 80.9 dB; wspr2 and Pico A exceeded 84.7 dB in
  every slot interior. The criterion is signal correspondence; these are not
  calibrated receiver noise, RF power or frequency-accuracy measurements.
- [Local status transitions](wtp-concurrent-fleet-part2-results/local-state-analysis.json),
  [verification assertions](wtp-concurrent-fleet-part2-results/verification.json),
  [restored controller](wtp-concurrent-fleet-part2-results/restored-controller.json)
  and [independent final device status](wtp-concurrent-fleet-part2-results/final-device-status.json).
- [Private evidence manifest](wtp-concurrent-fleet-part2-results/private-evidence-manifest.json):
  raw IQ and full observations remain on wspr5 at
  `/home/pi/wtp-managed-rp1-fix/part2/run2`; only compact evidence is in Git.
  Large transfers used tar piped through gzip.

Executed commands:

```sh
python3 -u /home/pi/wtp-managed-rp1-fix/part2/run2/fleet-part2-run.py /home/pi/wtp-managed-rp1-fix/part2/run2 1790888161
python3 /home/pi/wtp-managed-rp1-fix/part2/run2/analyze-part2.py /home/pi/wtp-managed-rp1-fix/part2/run2
python3 /home/pi/wtp-managed-rp1-fix/source/docs/development/wtp-managed-rp1-fleet-results/final-state-probe.py /home/pi/wtp-managed-rp1-fix/part2/run2/final-device-status.json
node docs/development/wtp-concurrent-fleet-part2-results/websocket-observer.cjs docs/development/wtp-concurrent-fleet-part2-results/local-websocket.jsonl 1790888442 /Users/lbussy/GitHub/WsprryPi/WsprryPi-UI/node_modules/ws/index.js
```

The final-state probe is retained in `wtp-managed-rp1-fleet-results`. Python
helper compilation, JSON parsing, source/deployment/harness hash comparisons,
unique-job and restoration assertions, and final whitespace checks passed.
No application or UI source changed, so the already passing source suites and
Impeccable review were not repeated. This turn exercised the actual managed
service and physical RF, rather than adding source-only qualification.

The installed controller binary and companion hashes still match the reviewed
deployment. Its version label contains the pre-commit base `6a32789`; the
changed-source manifest matches the code committed in `9a1b8c1`. Remote device
images were retained from the documented preparation; this is not a claim that
every member was rebuilt from the new controller commit.

## Review and restoration

[Adversarial review and reassessment](wtp-concurrent-fleet-part2-results/adversarial-review.md)
closed two harness issues: using the UI label `resume` instead of API operation
`enable`, and supplying a WebSocket Origin port different from its listener.
The preparation-only attempt was cleaned up before its RF window; its failed
capture and read-only connection are recorded separately. The corrected batch
passed. No application defect was found in this run.

The temporary service drop-in was removed and actual process arguments verified
as `/usr/local/bin/wsprrypi -J -i /usr/local/etc/wsprrypi.ini`. GPS/PPS,
receiver calibration and installed application/provider files were preserved.
The approved Fleet frequency override remains as in the pre-test state for the
remaining campaign. All five assignments are paused, local Enable is off and
DNS-SD is available. The current combiner connections can remain in place.

## Remaining wider acceptance

- Sustained scheduling/reconnection beyond these three clean rounds.
- Fleet assignment creation/editing through a browser connected to an actual Pi.
  Both local takeover choices already passed in the
  [wspr4 native recovery campaign](wtp-native-recovery-results/README.md).
- Discovery link loss, address changes, multihomed recovery and cache expiry.

Eight-way testing is excluded by the operator's explicit instruction. Fleet
acceptance uses the available five remote devices and central local output,
with two receiver groups constrained by the four-way combiner.
The separate wspr5 inbound WTP GPIO20 test is recorded in
[the RP1 endpoint acceptance report](wtp-rp1-inbound-report.md).

This pass does not claim those campaigns, WSPR decode, frequency calibration,
full RF-chain qualification or release readiness.

## Documentation Impact

- Updated: this acceptance report, compact evidence and historical-report links
  closing the formerly pending native-frame/second-group test.
- Considered and unchanged: `docs/wtp-fleet.md`, `docs/wtp-pi-operation.md` and
  the operator Fleet/local Enable pages reviewed in the implementation report.
  No operator behavior changed in this testing turn, so no operator-documentation
  or UI screenshot update is required. The sibling repository was not modified.
- No application/component paths were modified; the parent commit contains only
  development test records and reproduction helpers.
