# Support Bundle Intake Maintainer Runbook

This runbook is the durable operational companion to the
[private intake contract](support-bundle-private-intake.md). It covers key
custody, signed route administration, received-bundle processing, retention,
and abuse recovery. Operator-facing instructions live in the separate
`Wsprry_Pi_Docs` repository.

## Production Inventory

| Purpose | Key ID | Algorithm | SHA-256 fingerprint |
|---|---|---|---|
| Intake manifest signing | `wsprrypi-intake-2026-01` | Ed25519 | `688b5769d2b763481bad938fe8a9963693950c5e80bcf6d47d71db75711843ac` |
| Bundle encryption | `wsprrypi-bundle-2026-01` | age X25519 | `61289289afbd0f7813eb59b54e60d514f3cd8dbdf05e9c6b2d405b101b5b0fc4` |

The reviewed public metadata is versioned under
`config/support-bundle-intake/`. Compiled application trust is generated in
`src/support_bundle_intake_compiled_trust.hpp`. The public signed manifest lives
in [WsprryPi/support-intake](https://github.com/WsprryPi/support-intake) on
`main`; clients retrieve only `wsprrypi/intake.json` and
`wsprrypi/intake.json.sig`.

The Dropbox File Request is named **WsprryPi Support Bundles** and targets the
private `Support Bundle Intake/WsprryPi/Incoming` directory. Its opaque URL is a
capability and must not be committed, logged, pasted into an issue, or included
in this runbook. On the maintainer Mac it is stored in the login Keychain under:

- service `org.wsprrypi.support-intake`;
- account `wsprrypi-file-request`; and
- label `WsprryPi Dropbox File Request`.

## Private-Key Custody and Recovery

Private identities never belong in Git, the application, CI, Dropbox, logs, or
command output. Live files must be owner-owned regular single-link files with
mode `0400` or `0600` inside owner-only directories.

Recovery-qualified backups are separate unshared password-vault secure notes:

- `WsprryPi Manifest Signing Identity — wsprrypi-intake-2026-01`
- `WsprryPi Bundle Decryption Identity — wsprrypi-bundle-2026-01`

The signing-key attachment may be named
`wsprrypi-intake-2026-01.ed25519-private.pem.txt`; only the extension differs
from the live PEM filename. Restore attachments individually into a new mode
`0700` temporary directory, set file mode `0400`, verify the corresponding
public identity without printing private bytes, exercise signing or decryption
with synthetic data, and remove the temporary copy. Password-vault exports are
not a substitute for attachment recovery testing.

Provision replacement identities only with:

- `scripts/maintainer/provision_support_bundle_age_key.py`
- `scripts/maintainer/provision_support_bundle_intake_signing_key.py`
- `scripts/maintainer/preflight_support_bundle_intake_identity_ceremony.py`
- `scripts/maintainer/compile_support_bundle_intake_trust.py`

Use each tool's `--help` output as the current CLI authority. Key IDs use
`wsprrypi-bundle-YYYY-NN` and `wsprrypi-intake-YYYY-NN`. A key rotation requires
reviewed public metadata, recovery-qualified private material, compiled trust
for every still-recognized key, a compatible application release when trust
changes, and a newly signed manifest generation.

## Signed Intake Administration

The manifest expires and must be renewed before expiry. Every change increments
the generation. Never edit `intake.json` or its detached signature manually.

Use these tools for their named boundaries:

1. `manage_support_bundle_intake_manifest.py` authenticates the current staged
   generation and performs `inspect`, `renew`, `rotate`, or `disable`. Mutation
   requires the private signing identity, bundle metadata, explicit timestamps,
   and `--approve`.
2. `prepare_support_bundle_intake_production_manifest.py` is the fixed
   Keychain-backed **generation-1 bootstrap** retained for reproducibility; do
   not use it for later generations.
3. `commit_support_bundle_intake_publication.py` creates the exact local
   candidate in the owner-controlled bare publication repository.
4. `push_support_bundle_intake_publication.py` for the exact lease-protected
   push to `main` using an allowlisted credential helper.
5. `verify_support_bundle_intake_publication.py` to retrieve both public files,
   require exact equality with the staged source, and authenticate them.

First run mutating tools without `--approve` and review the typed proposal.
Approval must be deliberate and bound to that proposal. Treat
`committed_sync_uncertain` as unresolved: do not disclose or use a new route
until an identical retry confirms durability.

For ordinary request rotation, close the abused or obsolete Dropbox request,
create and signed-out-test its replacement, update the Keychain item, publish a
higher signed generation, and verify both old-link rejection and new-link
operation. Do not reopen an abused request. To suspend intake, publish a signed
`disabled` generation without a request URL. Raising
`minimum_upload_version` is a compatibility decision, not routine rotation.

## Receiving and Inspecting a Bundle

Acquire the Dropbox ciphertext and the user's separately downloaded receipt as
independent files. Do not trust the Dropbox filename: Dropbox may append uploader
identity. Do not extract or open the archive before inspection succeeds.

Run `scripts/maintainer/inspect_received_support_bundle.py` with explicit
absolute paths to:

- the ciphertext;
- receipt;
- matching `<bundle-key-id>.age-identity.txt`;
- an owner-only mode `0700` temporary work directory; and
- the fixed `/usr/bin/age` in production.

The inspector validates receipt structure, key ID, sizes and hashes, performs
bounded decryption, rejects unsafe tar members and excessive expansion, and
matches the internal manifest inventory without extracting diagnostics.
Successful output contains only safe correlation identifiers. A typed failure
is not a valid bundle and must not be promoted.

## Promotion and Confirmation

Use `scripts/maintainer/process_received_support_bundle.py` with explicit
owner-only `Incoming` and `Processed` directories, the exact inspected inputs,
identity, temporary work directory, and one retention class:

- `uncorrelated`: review after 14 days;
- `active_case`: no automatic review timestamp; or
- `resolved_case`: review after an explicitly selected 30–90 days.

Interpret transaction states precisely:

- `processed`: the canonical case is durable and exact Incoming objects were
  removed;
- `unchanged`: an identical retry confirmed the existing case;
- `processed_cleanup_pending`: Processed is valid but Incoming cleanup remains;
- `committed_sync_uncertain`: publication occurred but durability is not yet
  confirmed; retain Incoming and retry; and
- any pre-commit failure: Incoming remains authoritative and nothing is
  promoted.

Only after successful inspection and promotion should the maintainer post a
privacy-safe confirmation containing the case ID. Never attach or quote the
bundle, receipt, transfer URL, private contact information, callsign, locator,
network details, or diagnostic content in the public issue.

## Retention Audit and Deletion

Run `scripts/maintainer/audit_support_bundle_retention.py` with the absolute
Processed directory and an explicit canonical UTC time. It is read-only and
reports safe identifiers for due cases only after revalidating the complete
case transaction.

Delete exactly one eligible local case with
`scripts/maintainer/delete_expired_support_bundle.py`. Supply the Processed
directory, exact case ID, exact artifact ID, and the exact confirmation text
required by the tool. The transaction is restartable and reports cleanup or
sync uncertainty truthfully. Never script bulk deletion around it without a
separately reviewed design.

Local deletion does not erase Dropbox deleted-file history, synchronized
replicas, downloaded copies, backups, or the user's files. Review those systems
under their own retention policies before claiming erasure.

## Routine Checks

- Monitor manifest expiry and renew it before clients enter an unavailable
  state.
- Verify the Dropbox request periodically from a signed-out browser and confirm
  the destination remains private.
- Monitor Incoming storage and notifications; a File Request has no strong
  per-user denial-of-service control.
- Keep `age`, `age-keygen`, OpenSSL, and the maintainer scripts available and
  run their focused tests after dependency upgrades.
- Revalidate password-vault recovery after key rotation or vault migration.
- Preserve old private decryption identities while any retained ciphertext may
  still reference them.
