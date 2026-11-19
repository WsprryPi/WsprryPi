# Legacy retirement and native recovery


External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.
## Before replacing WTP-capable software

Complete retirement using the WTP-capable controller and target software before
replacing either binary. The reviewed source pair is Pi
`78c43577c80b45a56244fdb4536295d75e4538a7` and Pico
`ad697eaf982f35f554ffd319694837e901a32205`; these identify source candidates,
not published releases or installed-device qualification. Retain the actual
working binaries, compatible configuration, credentials, and recovery records
for the deployment until its duties are resolved.

1. Inventory each controller and target by full device and boot identity,
   profile, configuration revision, recurring assignment, ownership, and pending
   execution or cleanup duty. An offline target, stale observation, unknown
   controller inventory, or an already enabled native authority blocks that
   target's transition until explicitly resolved.
2. Use the existing controller-retirement and target-migration controls to
   persist the legacy recurrence fence. Verify the saved result after restart or
   reconnect; a disabled checkbox alone is not a retirement receipt. Do not
   replace the software while the state is retiring, reconciling, or unknown.
3. Let accepted work finish, or use the existing explicit stop action. Read
   terminal, inactive, and unowned output from the exact target. Timeout, lost
   acknowledgement, disconnect, process exit, or reboot does not establish
   cleanup. Keep uncertain work held without replaying LOAD or ARM.
4. Complete authenticated retirement for every recorded controller and retain
   its durable receipt. Reconcile pending cleanup and protected journals through
   their owning software; do not delete records or credentials to make a check
   pass. Other targets remain independently manageable.
5. Review the replacement against the original mode, message, frequency,
   period/phase, repetitions, timing limits, location/time prerequisites, and
   solar rules. Use only semantics supported by that target. An unsupported or
   lossy mapping needs an explicit supported operator choice; otherwise leave
   the replacement disabled. Never seed it from another controller's defaults.
6. Save the reviewed native schedule disabled, then validate and read it back
   from the same device, boot, profile, and revision. Resolve any uncertain write
   through authoritative readback before a new deliberate action. Enable only
   through the separate explicit native enable action under current authority.
7. Replace WTP-capable binaries only when the relevant deployment set has no
   remaining legacy dispatch authority or cleanup duty. Disable an inbound Pi
   WTP listener and remove active WTP backend selection explicitly while the
   old software can validate and preserve the configuration. Keep management,
   time, stop, credentials, receipts, and recovery access available throughout.

Devices without the required native recurrence or management remain on the old
software or need a separately authorized upgrade. A retirement build's refusal
of active legacy state is a secondary guard, not evidence that this procedure
has happened. Inactive connection settings are not dispatch authority and must
not be erased or normalized by autosave.

Rollback requires explicit native authority disable/retirement, confirmed output
reconciliation, and compatible configuration and authority restoration. Restoring
an older executable or INI alone does not authorize recurrence. Never
automatically re-enable the legacy schedule or discard the last usable recovery
binary while duties remain.

## Current recovery

Legacy rows and durable receipts are read-only in retirement software. They do not authorize reconnect, remote job inspection, or new dispatch. If prerequisite cleanup was not completed, keep the target held and restore compatible WTP-capable recovery software under an explicit authority restoration procedure. Do not delete records or automatically re-enable recurrence.

For native stop, preserve unknown state until an explicit authenticated readback verifies disabled schedules and inactive, unowned output. Use local Console safety recovery when the target cannot be reached. Settings drafts remain separate from saved or enabled schedules.
