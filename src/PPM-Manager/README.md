# PPMManager

PPMManager provides a provider-neutral snapshot of a system-clock frequency
estimate and its quality metadata. The current adapter reads Chrony's
machine-readable `tracking`, `sources`, and `sourcestats` reports. When the
provider is unavailable, the component reports that state instead of inventing
an estimate; the parent application owns its manual and uncalibrated fallback
policy.

Selected-source age is unknown when absent, malformed, negative, or associated
with an ambiguous selected source. Snapshots carry a monotonic acquisition
anchor; `getProviderSnapshot()` returns a copy whose age includes elapsed time
since collection. `withAgeAt()` supplies the same calculation for deterministic
consumers/tests and safely rebases already-aged copies. Invalid/missing anchors
do not establish freshness. The parent owns the current/stale limits and
correction fallback policy.

Provenance uses only the selected (`*`) and combined (`+`) source rows. Chrony
mode `#` identifies a local reference; `^` and `=` identify network sources.
Only a mixture of local and network sources is `Mixed`; unrecognized modes
remain `Unknown`. Source names and unselected PPS/GNSS rows cannot establish
local-reference provenance. This classification does not verify reference lock,
electrical routing, authentication or RF accuracy. See the official
[Chrony source report reference](https://chrony-project.org/doc/4.4/chronyc.html#sources).

The public component boundary is `src/ppm_manager.hpp` and
`src/ppm_manager.cpp`. `PPM_Weighting_Discussion.md` documents the estimation
and quality model.

## Building and hardware-free testing

From the WsprryPi checkout:

```sh
cd src/PPM-Manager/src
make release
make test
```

The fixed outputs are `build/bin/ppm-manager` and
`build/bin/ppm-manager_test`. Ordinary `make test` parses fixed Chrony report
fixtures. It does not query Chrony, inspect systemd, alter services or clock
configuration, request scheduler priority, use devices, or require elevated
privileges.

The retained standalone demonstration queries the host's live Chrony state and
waits for an operator interrupt. It is deliberately excluded from ordinary
testing and guarded behind explicit opt-in:

```sh
make live-test PPM_MANAGER_LIVE_TEST=YES
```

Installing, starting, stopping, or configuring Chrony is outside the component's
ordinary test boundary.

## API outline

- `initialize()` obtains the current provider snapshot.
- `getProviderSnapshot()` returns the estimate, synchronization, age, skew,
  residual frequency, source state, provenance, and error reason.
- `parseChronyReports()` parses supplied machine-readable reports without
  consulting host services; this is the hardware-free test boundary.
- Optional C++ acquisition/consumption timestamps are transient test/API inputs;
  no CLI, INI, environment or UI calibration controls are added.
- `startPPMUpdateLoop()` and `stop()` manage periodic live-provider refreshes.
- `setPPMCallback()` registers an estimate callback.

## Component and extraction boundary

This directory is ordinary tracked content in the WsprryPi repository. Keep the
source hierarchy, standalone Makefile and demo, parser test, README, and
weighting discussion together.

For standalone extraction, copy `src/PPM-Manager`, initialize a new repository,
and add an appropriate license file.

## License

While part of WsprryPi, this component is covered by the repository-root
`LICENSE.md` (MIT).
