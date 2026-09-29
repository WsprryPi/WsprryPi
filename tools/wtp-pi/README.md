# Pi endpoint validation probe

`probe.py` uses only the Python standard library. Read-only example:

```sh
python3 tools/wtp-pi/probe.py --host wspr4.local --output /tmp/pi-inspection.json
```

Job cases require `--execute`. Physical cases additionally require `--allow-rf`
after operator authorization. Each case uses one finite 20 m tone and records
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
