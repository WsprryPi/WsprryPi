# WsprryPi Support Bundle Private Intake Contract

Maintainer operations are documented in the
[Support Bundle Intake Maintainer Runbook](support-bundle-maintainer-runbook.md).

## Purpose

This contract defines how a user can generate a support bundle locally, inspect it, associate it with useful support context, encrypt it for the maintainer, and upload it through a private Dropbox File Request.

Bundles may contain identifying or operational information such as email addresses, amateur-radio callsigns, Maidenhead locators, hostnames, configuration, and private-network addresses. They are not intended to contain passwords, authentication tokens, private keys, or other secrets.

## Security and Identity Model

The workflow provides:

- public-key confidentiality for the uploaded bundle;
- integrity checking of the readable archive and encrypted upload;
- a readable local artifact the user can inspect before sharing;
- private transfer without publishing the maintainer's email address;
- correlation between the bundle and a support case; and
- an authenticated, replaceable upload destination.

It does not attempt to prove the uploader's identity or contact information, prevent deliberate diagnostic modification, hide upload metadata from Dropbox, guarantee availability against a determined denial-of-service attack, or permit anonymous GitHub issue creation.

The application-generated manifest records what the application collected. It is not an identity attestation.

## Project and Key Separation

WsprryPi SHALL have its own:

- bundle-encryption key pair;
- support-intake manifest-signing key pair;
- Dropbox File Request and `Incoming` and `Processed` directories;
- project identifier; and
- documented public-key identifiers and fingerprints.

Bundle encryption and intake-manifest signing SHALL use separate key pairs.

| Function | Private key used by | Public key used by |
|---|---|---|
| Bundle encryption | Maintainer during decryption | Installed application |
| Intake-manifest signing | Maintainer administration tool | Installed application |

Private keys:

- MUST remain under the maintainer's control;
- MUST NOT appear in the application, repository, installer, documentation, CI, logs, Dropbox, or published manifest;
- SHOULD be stored in the macOS Keychain when practical;
- SHOULD have an encrypted backup in the maintainer's password vault;
- MUST be rotated if compromise is suspected; and
- SHOULD be retained while related artifacts may still need validation or decryption.

Public keys MAY be distributed with the application or public repository. Each MUST identify its project, purpose, and version and have a documented fingerprint.

## Bundle Contents and Manifest

The application SHALL collect only information reasonably relevant to support. Known passwords, tokens, cookies, OAuth credentials, SSH private keys, encryption or signing private keys, and password-vault contents MUST be excluded or redacted.

Potentially identifying diagnostic categories MAY be included when relevant, but MUST be disclosed before upload. These include email addresses, callsigns, locators, hostnames, usernames, internal IP addresses, device identifiers, logs, and configuration.

Every version-1 readable bundle SHALL contain a machine-readable manifest with
exactly these top-level fields:

```text
schema_version
contract_version
project_id
project_version
case_id
created_at_utc
collection_options
privacy_categories
support_context
collection_warnings
files, with relative path, size, and SHA-256
```

The v1 manifest deliberately contains no installation identifier, encryption key,
recipient, fingerprint, or upload destination. Those values are not needed to
inspect the readable archive and some are selected only after finalization.
Collection errors, omissions, and partial results SHALL be recorded.

## Local Creation, Review, and Finalization

Bundle generation SHALL produce:

1. A readable local archive for the user.
2. An encrypted upload artifact containing the exact bytes of that archive.

The current WsprryPi `.tar.gz` format MAY remain the readable artifact; this contract does not require ZIP.

The readable archive SHALL be saved locally before upload. The application SHALL show its filename or download result, size, case ID, included data categories, collection warnings, and a warning that the bundle may identify the user, station, equipment, or network. It SHALL offer local download or saving and separate controls to continue or cancel.

The application SHOULD encourage inspection. It MUST NOT prevent the user from opening the readable artifact or upload merely because collection completed. Upload requires separate, explicit consent.

The required sequence is:

```text
choose collection options and support context
collect
build readable archive
present review
either cancel/recollect with changed options, or approve the retained candidate
finalize readable archive
hash the exact readable archive bytes
encrypt those exact bytes
upload only the encrypted artifact
```

The application MUST NOT recollect diagnostics between review and encryption.

## Integrity Terminology and Local Receipt

The application SHALL calculate the finalized readable archive's SHA-256 digest
and SHALL provide a receipt containing exactly the v1 fields:

```text
schema_version
project_id
case_id
artifact_id
created_at_utc
archive_filename
archive_size
archive_sha256
encrypted_filename
encrypted_size
encrypted_sha256
bundle_encryption_key_id
issue_url, nullable
upload_state = encrypted_artifact_downloaded
```

Documentation and UI MUST distinguish a hash, a digital signature, and encryption. A hash alone MUST NOT be called a signature. A bundle application signature is optional and, if implemented, attests to the manifest and file hashes rather than the user's identity.

## Bundle Encryption

After review and consent, the application SHALL encrypt the exact readable archive bytes with the WsprryPi bundle-encryption public key using a standard format such as `age`.

It SHALL record the key identifier, require successful non-empty exact-size and
SHA-256-verified output, retain the readable archive unless the user deletes it
explicitly, and never upload the readable archive. Real packaged-`age`
encrypt/decrypt fixtures validate the format; the application does not
claim that superficial ciphertext parsing proves decryptability.

Recommended encrypted filename:

```text
wsprrypi-support-<case-id>-<artifact-id>.tar.gz.age
```

The encrypted manifest is authoritative. Dropbox may alter the stored filename.

## Required Support Context

Before upload, the user SHALL provide one of:

- an existing GitHub issue reference;
- intent to create a new GitHub issue, plus a meaningful description and
  user-approved contact method until that public issue exists; or
- a meaningful problem description and contact method.

A bundle MUST NOT be uploaded with no GitHub correlation, no useful description, and no contact method. An explicitly anonymous submission MAY be permitted only when the project has a documented no-follow-up workflow.

## GitHub Workflow and Authentication Boundary

For an existing issue, the application SHALL accept an issue number or URL and SHOULD validate the repository and existence before recording the normalized reference in the encrypted manifest.

After upload, it SHOULD offer to open the issue with this prepared, user-approved comment:

```text
A support bundle was generated and uploaded through the private support channel.

Case ID: <case-id>
Project version: <version>
Operating system: <operating-system>

No diagnostic bundle or transfer link is attached to this public comment.
```

When no issue exists, the application SHOULD offer a prefilled issue containing only the case ID, project version, operating system, and user-approved public problem description. It MUST warn against pasting diagnostics, transfer links, email addresses, callsigns, locators, or network details into the public issue.

GitHub requires an authenticated user or authenticated intermediary to create or comment on an issue. The application MUST explain that sign-in is required and MUST NOT contain maintainer GitHub credentials or create issues automatically. WsprryPi does not provide an anonymous issue-creation intermediary.

If the issue is created after encryption, the case ID SHALL correlate it with the
bundle. The version-1 downloaded receipt is immutable and is not retroactively
rewritten; it retains the issue reference known when encryption began.

## Non-GitHub Workflow

If the user cannot or chooses not to use GitHub, the application SHALL require a meaningful problem description and user-approved contact method, include both inside the encrypted bundle, use the case ID for correlation, and explain that no public issue will be created.

Dropbox's required name and email fields SHALL NOT replace support context inside the encrypted bundle because their owner-side availability may depend on Dropbox plan or interface behavior.

## Dynamic Support Intake Configuration

The application MUST obtain its upload destination from a remotely published, cryptographically signed intake manifest. It MUST NOT treat a build-time Dropbox URL as permanently valid.

The configuration SHALL be published as exact-byte-signed files:

```text
intake.json
intake.json.sig
```

The detached signature SHALL cover the exact published bytes of `intake.json`. The application SHALL pin a public key used exclusively for intake-manifest verification.

The version-1 manifest SHALL contain exactly:

```text
schema_version
project_id
generation
published_at
expires_at
minimum_upload_version
minimum_client_protocol
request_url, when active
release_url
status
user_message, nullable
bundle_encryption_key_id
```

The signing key ID is carried by the detached signature envelope together with
the Ed25519 algorithm and signature. It is not duplicated inside `intake.json`.

Before every upload attempt, the application SHALL retrieve the manifest and signature and verify the exact-byte signature, project ID, supported schema, expiry, minimum application version, and active status. It SHALL use the request URL only if every check succeeds.

The application MUST fail closed for a missing, invalid, expired, incorrectly signed, wrong-project, disabled, or incompatible manifest. Retrieval failure SHALL be reported as a network or availability problem, not automatically as an upgrade requirement. Local collection, review, and saving SHOULD remain available.

## Intake Status, Rotation, and Upgrade Gating

The manifest SHALL support `active` and `disabled`. An active manifest SHALL contain a valid request URL. A disabled manifest SHALL omit the URL and SHOULD explain that uploads are temporarily unavailable while local bundle creation remains available.

For ordinary request rotation:

- the maintainer SHALL close the old request;
- create a new request;
- increment the manifest generation;
- publish the replacement URL in a newly signed manifest; and
- verify old-link rejection, signed-out replacement upload, correct destination, published signature, and a supported application workflow.

A closed request SHALL NOT normally be reopened after abuse because reopening restores its public URL. Compatible applications SHALL follow an ordinary rotation without upgrading.

The maintainer MAY raise `minimum_upload_version` when older clients must no longer upload. An older application SHALL preserve local generation and review, block upload without a bypass, show the minimum version, offer the official release location, and avoid revealing or opening the replacement URL.

Recommended upgrade-required copy:

```text
Support bundle upload has changed

This version of WsprryPi can still create a local support bundle, but it can no longer upload one securely.

Upgrade to version <minimum-version> or later and try again.

Your local bundle has not been uploaded or deleted.
```

Recommended actions are **Download Latest Version**, **Save Local Bundle**, and **Cancel**.

Applications released before dynamic intake configuration cannot display this project-specific warning. Dynamic configuration SHOULD therefore ship in the first WsprryPi release that supports uploading.

## Manifest Publication and Expiration

The manifest and signature MAY be public; their signature provides authenticity, not secrecy. A maintainer-only tool SHOULD provide `inspect`, `rotate`, `disable`, and `renew` operations rather than requiring manual JSON or signature editing.

The tool SHOULD validate URLs, read the current generation, set timestamps and minimum version, create deterministic bytes, sign and self-verify, preview the exact change, publish only after explicit approval, and retrieve and verify the published result.

The manifest SHALL expire. An initial policy MAY use 90-day validity and a reminder 14 days before expiration. Renewal increments the generation and creates a new signature without requiring URL rotation. Expiration blocks upload but not local bundle creation.

## Dropbox Upload and Privacy Disclosure

WsprryPi SHALL use a Dropbox File Request directed to its private `Incoming` directory. It SHALL accept signed-out uploads without exposing the destination, previous submissions, or maintainer email address. It SHOULD notify the maintainer and be closed and replaced if abused.

Before opening Dropbox, the application SHALL disclose:

```text
The encrypted support bundle will be uploaded using Dropbox.

Dropbox will ask for your name and a valid email address. Dropbox and the WsprryPi maintainer may receive this information as upload metadata. A Dropbox account is not required.

Dropbox cannot read the encrypted bundle contents, but it can observe the filename, file size, upload time, network information, and the name and email address entered on its upload form.
```

The application SHALL open only the project File Request, never a browsable shared folder. The URL MUST NOT grant browse, download, edit, or delete access.

Dropbox may append the submitter name to the stored filename. Exact filename matching is therefore prohibited; the encrypted manifest is authoritative. Dropbox names are personal metadata and MUST NOT be copied automatically to public issues.

## Upload State and Completion

The application SHALL distinguish:

```text
Bundle created
Bundle encrypted
Upload page opened
Upload reported complete by user
Upload confirmed by maintainer
```

Opening the Dropbox page is not upload success. The local receipt MAY record completion after the user confirms Dropbox's success message. Maintainer receipt or notification establishes confirmation.

Recommended completion copy:

```text
You reported that Dropbox displayed upload success for your encrypted support bundle.

Case ID: <case-id>
Local bundle: <path>
Local receipt: <path>

Keep the case ID for future correspondence. The maintainer has not yet confirmed receipt.
Your readable local bundle has not been deleted.

If you use GitHub, continue to the issue now. Do not attach the diagnostic archive or encrypted file to the public issue.
```

The completion view SHOULD provide **Open Existing Issue**, **Create GitHub Issue**, **Copy Case ID**, **Save Local Bundle**, **Delete Local Encrypted Artifact**, and **Finish**.

## Maintainer Intake and Retention

For every received artifact, the maintainer SHOULD pair it with the separately
downloaded receipt; decrypt it with the project key; verify the ciphertext and
readable archive sizes and hashes, project, case ID, format, and key ID; reject
absolute paths, traversal, symlinks, and excessive expansion; inspect before
opening individual files; correlate the support context; promote it from
`Incoming` to a canonical `Processed` case; and assign retention.

Dropbox may append uploader identity to its stored filename. The processor SHALL
therefore treat that name as untrusted personal metadata and SHALL NOT copy it
into the canonical processing record. Canonical correlation comes from the
validated receipt and encrypted manifest, not the provider filename.

Decryption or integrity failure MUST be reported and MUST NOT be treated as valid.

Recommended retention defaults are:

- uncorrelated or unusable submissions: 14 days;
- active cases: while active;
- resolved cases: 30 to 90 days unless justified otherwise;
- decrypted working copies: only as long as needed; and
- the user's local archive and receipt: under user control.

Deleting from Dropbox may not remove downloaded, synchronized, or backed-up copies. Maintainer documentation SHALL describe actual storage behavior.

## Abuse Controls

A public File Request is not a strong anti-abuse boundary. The project SHALL bound bundle size, logs, and collection time; compress before encryption; use one request per project; separate incoming and processed storage; monitor storage; support URL rotation; retain no longer than policy permits; and never execute received content automatically.

The application SHOULD require confirmation or impose a local cooldown before repeated uploads. Dropbox does not provide an application-controlled per-user quota. Stronger rate limiting would require a controlled broker or commercial service.

## Failure Behavior

- Collection failure: do not upload; identify partial results and explain omissions.
- Encryption failure: retain the readable archive; do not open Dropbox or fall back to plaintext.
- Intake-manifest failure: report availability or validation failure; do not use stale or unverified routing.
- Obsolete application: retain local creation and review; block upload and offer the official upgrade.
- Dropbox failure: retain the encrypted artifact, case ID, and receipt for retry; never suggest a public plaintext attachment.
- GitHub failure or refusal: continue only with sufficient description and contact information.

Operator instructions live in the separate `Wsprry_Pi_Docs` repository. This
repository retains only the normative contract and durable maintainer runbook.

## Deliberate Operational Limits

- Local deletion cannot erase Dropbox history, synchronized replicas, backups,
  downloaded copies, or user-held artifacts.
- Retention deletion is one explicit case at a time; there is no automatic bulk
  deletion or automatic active-to-resolved transition.
- GitHub authentication remains user-controlled. There is no anonymous issue
  proxy and no embedded GitHub or Dropbox credential.
- Provider behavior and the signed request route must be revalidated when the
  Dropbox request is replaced, account behavior changes, or a new intake
  generation is published.
