<!-- SPDX-License-Identifier: MIT -->

# RP1 candidate acceptance requirements

Candidate acceptance binds one exact WsprryPi source revision to one exact
unpublished provider source revision and its reproducible assets. The
[candidate lock](../../scripts/tests/fixtures/rp1-candidate-1.0.0.json) is the
machine-readable authority for source, tree, package, UAPI, overlay and asset
identities. The [acceptance command](../rp1-candidate-acceptance.md) rebuilds and
independently validates those inputs without installation or target access.

## Software evidence

Use clean committed consumer and provider inputs for final acceptance. Keep
candidate assets, logs, private configuration, target captures and results
outside Git. Results must identify both source revisions, tool/lock/installer
hashes and any working-tree changes. Reject substituted, missing, duplicated,
malformed or unexpected assets. Unpublished candidate acceptance never grants
production release selection or establishes publication or RF qualification.

Required software coverage includes exact-source development selection,
production refusal of unpublished releases, distinct GPIO4/GPIO20 identities,
cleanup-first predecessor removal, interrupted inverse recovery, ownership and
rollback refusal, bounded provider failures, complete eight-second TONE plans,
chunk-boundary cancellation, owner close, service stop and cleanup fault latches.
Owner-close drain completion must clear both owner and lease; passive terminal
history must remain observable. Stale callbacks must not regain authority.
All RP1-supported modes require their applicable planning and lifecycle checks.

Repeat affected provider build/package/lifecycle checks and consumer checks
whenever a provider repair creates a new candidate. Kernel header builds prove
build compatibility only. Disposable package tests, simulated application tests,
provider boundary tests and hosted CI are separate evidence classes. None
substitutes for target installation, service, GPIO, DMA or RF evidence.

## Target evidence

A target plan identifies the configured SSH alias, exact consumer/provider
commits, starting ownership and package state, migration, deployment, route and
service operations, finite programs, stopping procedure, cleanup, rollback and
final state. Live output also requires identified frequency, duration, correction,
drive strength, route, attached hardware and physical RF path. Keep GPSDO output
sequencing explicit when that equipment is connected. Do not infer calibration
from an attached GPSDO, PPS timestamps or an uncalibrated SDR observation.

Use a fresh finite campaign ledger for each exact-input plan. Record every
attempt before starting output; enforce the approved program/time budget and
stop the sequence on a failure. A stopped campaign provides no retry authority.
Preserve raw failures and separately record any evidence-based adjudication.
Late STOP may race natural completion; accept its actual terminal reason only
when operation history, cleanup and receiver evidence agree. Record actual STOP
send timing rather than claiming the intended timer as observed timing.

For each route independently, verify the full eight-second interval, STOP near
start and chunk boundaries, owner close and service stop. Verify terminal reason,
endpoint closure, owner/lease clearance, stable GPIO/clock/DMA quiescence and
absence of successor output. Do not transfer evidence across routes or builds.
A cleanup failure remains visible and latched even when some resources appear
quiescent. Use provider-owned recovery and inverse removal on failure.

Capture rollback inputs before mutation, including the startup-file ownership
receipt. Restore managed startup files through their owning installer while the
current files still match that receipt; verify receipt/file agreement before
restoring other captured application state. Reinstall a predecessor through its
owning lifecycle and verify the resulting module identities and final service,
configuration, endpoint, route and GPSDO state. Historical runtime journals must
not be replayed as proof of restoration.

## Integration boundary

Acceptance remains incomplete while required target evidence or an actionable
adversarial finding remains unresolved. Complete a separate adversarial review,
repair every finding and repeat affected checks. Keep application acceptance,
provider acceptance, operator documentation and licensing reviews explicit.
Operator documentation lives in the separate documentation repository and has
its own authorization and validation boundary.

Passing acceptance does not authorize a PR, merge, provider freeze, tag,
publication or WsprryPi release. Integration into `devel` and later provider
freeze/publication are subsequent, separately authorized steps.
