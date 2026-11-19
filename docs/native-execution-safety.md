# Native execution and output safety

Each device owns its recurring schedule and executes locally. A Pi uses its explicitly selected native backend; a Pico uses its standalone scheduler. Management configures and observes the selected device. It does not upload finite jobs or dispatch occurrences to another transmitter.

## Internal execution contract

The Pico scheduler submits typed, bounded requests to the existing JobService and engine. Internal `wtp` types and namespaces remain implementation details; they are not a supported external protocol. A request is immutable and belongs to one full device/boot/owner context. Names, catalog handles, and connection state do not establish identity or authority.

The shared event profile uses integer nanoseconds and integer nanohertz. Events start at offset zero, have positive durations, remain contiguous and ordered, and exactly fill the declared duration. RF-on events specify a positive frequency; RF-off events omit it. Arithmetic must fit unsigned 64-bit bounds. Validate event count, complete duration, supported mode, engine range, and realization policy before admission. The current Pico execution descriptor supplies the actual bounds, including the 512-event ceiling and smaller configured duration limits. Never truncate, split, stretch, or silently substitute an unsupported request.

Frequency placement belongs to the native producer. Pi WSPR derives RF center from dial plus 1,500 Hz and one per-plan random placement; both frames of a paired plan retain that placement. Pico standalone produces its own native grid. An accepted absolute event receives no second dial offset or host calibration. Requested frequency, engine realization, and measured RF remain separate observations. Device calibration does not authorize changing an immutable accepted request.

## Ownership and terminal state

All local producers share one exclusive ownership service. A management disconnect or expired session cannot retime armed or running execution. Ownership remains through terminal handling; release preserves retained observations. A loaded request can be cancelled, but release is refused while armed, running, or in an unresolved output fault. Foreign ownership cannot be taken over by schedule configuration or stop through an unrelated management context.

The engine progresses through empty, loaded, armed, running, and complete/aborted/missed/failed observations. Completion and cancellation command output off and require bounded confirmation. Failure to confirm inactive output latches failed/unknown; a terminal state alone is never proof that output is inactive. New admission and authority release require the appropriate confirmed inactive state. Only local engine recovery can clear a failed output latch after verifying inactive output.

Retries cannot turn a retained terminal request into another execution. Internal request/job identities and content bindings prevent conflicting reuse. Retained results are bounded; absence or expiry of an observation does not establish successful completion. Unknown work stays held without load/arm replay.

## Clock admission and launch

Management and time provisioning remain available without qualified UTC. Execution independently requires the current clock state, uncertainty, holdover age, minimum lead, horizon, and leap policy. All interval arithmetic is checked. Unknown leap state and overlap with the exclusion window around a pending leap transition refuse admission.

An accepted UTC start remains immutable. Before launch, the device may refine its monotonic mapping using a newer admissible clock sample, within the original deadline. It rechecks clock validity and uncertainty before enabling output. It never deliberately starts early. A delayed launch must precede the exclusive end of the UTC second containing the requested start; this is not a rolling one-second allowance. A missed deadline leaves output disabled and does not retry the occurrence. Timer quantization rounds upward within that same second. Once running, the complete event sequence uses monotonic timing; UTC correction or loss of synchronization does not alter its duration.

Console launch counters are implementation diagnostics, not electrical edge measurements. Read current authenticated status for ownership and output observations. Software fixtures qualify these contracts only, not hardware timing, frequency accuracy, GPIO, services, installation, or RF.

## Native schedule stop

Authenticated GET/POST `/api/control/v1/schedules/stop` reads or stops both Pico native schedules. A mutation binds the current full device/boot target and exact revision, requires current authorization, uses one explicit POST, and returns confirmed disabled, inactive, unowned state. It uses the existing engine cleanup path and preserves settings, recurrence cursors, and recovery records. Persistence failure remains held; a foreign owner or unresolved output refuses success.

A lost response is unknown. Preserve drafts and use explicit stop-status readback from the same target before another action; never replay a stop mutation automatically. Ordinary status refresh does not resolve an uncertain stop. Local USB Console STOP remains a separate trusted safety control with its existing restart-cleared behavior. Re-enabling after native stop requires an explicit current settings read and deliberate native enable.

## Authority and migration

Retain protected management/retirement channels, credentials, durable receipts, and recovery journals. Stored active WTP backend selection, enabled inbound listener, or unresolved legacy recurrence/cleanup duties refuse affected execution. Inactive dormant WTP connection values remain private storage data and must not be erased by ordinary autosave. Complete retirement with WTP-capable software before replacing it; see [migration and recovery](wtp-status-recovery.md).
