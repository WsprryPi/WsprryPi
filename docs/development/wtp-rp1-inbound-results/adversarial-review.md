# RP1 inbound acceptance: adversarial review and reassessment

2026-10-01. wspr4 client, installed wspr5 service, RP1 GPIO20, five finite jobs.

## Findings and resolutions

| Finding | Resolution and evidence |
| --- | --- |
| Prior local wspr5 WSPR acceptance was treated as if it also covered the inbound adapter. | Explicitly distinguish the paths. Five new jobs originated on wspr4's `192.168.1.120` socket and completed on wspr5 through WTP. TONE, three keyed/shifted fixtures and a canonical full WSPR frame have corresponding SDR observations. |
| Eight simulated targets were retained after the operator specified the finite inventory and combiner limits. | Remove eight-way testing from active acceptance. Preserve historical records as superseded snapshots; the agreed physical Fleet scale is five remote members plus central local work, observed in two groups. |
| Browser takeover and Mac/network CI failures were repeatedly listed as open after later passes. | Reconcile against the actual wspr4 native recovery report and refreshed CI job records. The remaining browser gate is Fleet assignment creation/editing. The release build cancellation is separate from the passing Mac/network/Linux jobs. |
| RP1 confirmation cannot be generic or inferred from a previous local job. | Record actual launch arguments, exact selected GPIO20 and a distinct confirmation operation ID matching each inbound job. Five distinct service boot IDs and five successful LOAD/ARM/completion/RELEASE records are independently asserted. No authorization/provider gate was bypassed. |
| Merely accepting ARM or seeing output active does not prove completion or cleanup. | Retain complete terminal records, known-off release status and corresponding event-interior/post-job RF contrast; independently probe all six endpoints after service restoration. Five receiver captures report verified cleanup and no overruns/timeouts/clipping. |
| The stock probe's WSPR fixture is a cyclic four-tone diagnostic, not a canonical callsign frame. | Use the existing 162-symbol AA0NT/EM18/20 dBm frame and normal 110.592-second timeline. Check all event interiors. Make no decoder or calibrated RF-edge claim. |
| Preparation transfer used an incorrect relative source path for the probe. | The partial transfer did not execute RF. Transfer the tracked probe separately, then run the campaign. A later read-only monitor used a nonexistent probe path; its failed read did not mutate output or contribute a pass. |

## Reassessment

`verify.py` independently checks the five-mode set, unique job IDs and fresh
boots, job/confirmation binding, actual remote socket origin, advertised device
identity, successful LOAD/ARM/RELEASE, running observations, terminal completion,
receiver success/cleanup, WSPR duration/count, six inactive endpoints and
unchanged installed binary/helper/INI/assignment hashes. The RF analyzer checks
corresponding signal for every on-event interior, keyed gaps where present and
post-job contrast, and recomputes raw IQ hashes against receiver metadata.

All assertions passed. The normal service is restored with no temporary RP1
confirmation argument; local Enable is off, all five Fleet schedules are paused
and all six physical endpoints are empty, unowned and inactive. The route and
all five finite inbound modes are physically accepted under the existing RP1
development authorization profile. No additional actionable defect was found
within this scope. Existing all-band backend capability/qualification claims
were not reduced by this 20m test, and no new all-band measurement is claimed.

The remaining agreed tasks are recurrence across reconnections, live-browser
Fleet creation/editing, discovery topology/cache changes and completion of the
canceled GCC release CI job. Eight-way testing is excluded. Ordinary source
suites were not repeated because no application/component/UI source changed.
