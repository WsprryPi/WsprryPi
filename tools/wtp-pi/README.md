# Pi endpoint validation probe

`probe.py` uses only the Python standard library. Read-only example:

```sh
python3 tools/wtp-pi/probe.py --host wspr4.local --output /tmp/pi-inspection.json
```

Job cases require `--execute`. Physical cases additionally require `--allow-rf`
after operator authorization. Cases default to a finite 20m tone and record
WTP frames and HTTP state. Cases are `job`, `end-now`, `finish`, and
`http-enable`; use `--duration 3` through `10` to leave time for takeover.
The probe requests cleanup and disables local scheduling in `finally`.
It changes the selected test instance's local Enable setting. Use an isolated
INI and restore the prior service after a live campaign. The probe does not
install a binary or manage services.

Example for an explicitly simulated endpoint:

```sh
python3 tools/wtp-pi/probe.py --host wspr4.local --case finish --duration 5 \
  --execute --output /tmp/pi-finish.json
```

The physical acceptance criterion for this execution is a corresponding SDR
signal. Protocol/HTTP success alone is not that evidence. This probe does not
measure spectrum, absolute frequency accuracy, or the complete RF chain.

Use `--mode tone|wspr|qrss|fskcw|dfcw` to exercise mode-specific RF-event
fixtures, and `--duration` from 1 to 120 seconds. WSPR uses a repeated four-tone
162-event fixture; use `--duration 110.592` for normal WSPR symbol timing.
It is not an encoded callsign frame; these probes validate the
remote execution adapter, not message encoding or reception. The existing
controller encoder tests remain the message-format checks. Lease renewal keeps
longer probes owned. Completion requires protocol Complete and output inactive.

`capability_matrix.py --host HOST --port PORT --execute --output RESULT.json`
checks CAPS and LOAD/ABORT across 75 Si5351 band/mode combinations, then three
partial-tone-set jobs with leading silence and terminal RF-off. It claims the
idle target and may perform backend output-disable operations, but never sends
ARM or starts RF. Run only against an explicitly authorized test target.
