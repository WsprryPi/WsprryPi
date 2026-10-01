# Physical GPIO4 power-failure acceptance

October 1, 2026, `devel`. **Passed** for the bounded power-loss/recovery case.
The operator physically removed and restored wspr4 power during an actual Fleet
job. No shutdown command was used to substitute for power removal.

## Results

| Observation | Result |
| --- | --- |
| Active job | GPIO4, requested 14.097100 MHz TONE, 180 seconds. Job `14eb683689d25cb40000000000000001`, slot `1790852560000000000` UTC ns. |
| Receiver | Corresponding SDR signal rose from the noise floor. One interrupted signal segment; peak FFT SNR 90.746 dB. |
| Physical cut | Last strong FFT window at UTC `1790852643.542` s; first passive target-unavailable observation at `1790852643.9801402` s. The operator confirmed removal. |
| Target return | Stable device ID `f21e530af8a4a210ec57898c0ebd32d2`; WTP boot changed from `3ecb548ea8bf90649891f8c4b1d6d008` to `19d88d5752e3f3ad9765815d3ccf16eb`. Linux boot ID also changed. |
| Safe boot | Managed service active and enabled. Output known off, no owner or job, local Enable off, takeover generation still 4. Candidate and configuration hashes unchanged. |
| Controller | Consumed slot retained, interrupted work blocked, no second active job or slot. Explicit reconciliation cleared `in_flight` while keeping the assignment paused, then the assignment was removed. |
| Post-boot observation | Target remained idle for 96.48 seconds in passive records. All retained SDR windows after observed return were below the signal threshold. |
| Cleanup | SDR capture completed with exit 0 and verified device/stream cleanup. Both original managed service units active; wspr5's original binary and complete configuration restored. wspr4 retains the idle GPIO candidate. |

[Acceptance data](acceptance.json), [passive summary](observation-summary.json),
[SDR observations](sdr-observations.json) and [artifact hashes](evidence-manifest.json)
record the result. Raw passive logs and the 720 MB IQ recording remain on wspr5
at `/home/pi/wtp-power-loss.sV4Is9lV`. The pre-test target backup is also retained
there and on the development Mac; no older takeover journal was restored.

## Review and practical limits

- Review and reassessment ran in this thread. Assertions checked one active job
  ID, one consumed slot, fresh Linux/WTP boots, preserved configuration and
  generation, no post-boot ownership/output, explicit paused recovery, assignment
  removal, corresponding RF and verified receiver cleanup.
- The central test unit's 420-second runtime guard expired after the observation
  window, safely restoring the original wspr5 service. Its retained assignment
  was disabled with `in_flight=true`. The controller was restarted solely to
  reconcile and remove that record, then stopped. Continuous controller uptime
  through final reconciliation is not claimed.
- Cue timestamps were recorded after message delivery. FFT and passive HTTP
  windows bracket the observed cut; they are not precise physical edge timing.
- Target return occurred after the original 180-second window. This establishes
  safe cold boot and no replay of the retained interrupted slot in the observed
  period; it does not establish restoration before that job's nominal end.
- SDR visibility is the authorized RF acceptance criterion. No decode,
  calibrated-frequency, spectral-purity or complete RF-chain qualification is
  inferred. The separate approximately 32-second forced-process-death boundary
  remains unchanged.

No unresolved defect was found in this physical power-loss/recovery case. This
closes the final case in the recorded live failure-recovery campaign.

## Remaining gates and documentation impact

The source commit's CI run `36791292993` completed with full Linux validation,
release and strict I2C jobs passing. Mac `wtp-pi-tone-engine-test` failed its
completion assertion, and the network job's browser integration timed out
starting Chromium. Both CI issues remain open; this physical result does not
resolve them.

Broader acceptance still includes RP1 physical WTP, eight simultaneous targets,
a mixed physical Pi/Pico fleet, sustained scheduling/reconnection, live-browser
Fleet editing and discovery topology/cache changes.

Application development evidence and the campaign status were updated. Operator
backend, takeover and Fleet documentation was reviewed and left unchanged:
this run confirms existing behavior and introduces no new settings or workflow.
No application, UI or reusable component source changed; no source suite was
rerun for these evidence-only edits. JSON/evidence assertions, artifact hashes
and final whitespace checks were used for this closure.
