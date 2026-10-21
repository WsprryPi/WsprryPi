<!-- SPDX-License-Identifier: MIT -->

# RP1 qualification evidence and status

WsprryPi ships the RP1 route-evidence bundle with the application. Installation,
updates and status do not download a separate qualification release. The helper
checks the bundled files against the maintained
[reference](../release_tools/rp1-qualification-lock.json) before interpreting them.
Provider identity, structural compatibility, route readiness, scoped evidence,
application eligibility and current output state remain separate facts.

## Evidence identity and scope

The [bundle](../release_tools/rp1-qualification/) contains `rp1-qualification.json`,
`rp1-qualification.schema.json`, `qualification_status.py` and `SHA256SUMS`.
These files are reproduced byte-for-byte from provider metadata source
`bb6815446beb9b96853ad5a353b0d355d4975710` and retain their existing SHA-256
identities. Resolve that source by its full commit. The metadata's `metadataTag`
field is a stable evidence identifier; the application consumes the bundle locally.
The original [software lock](../scripts/tests/fixtures/rp1-release-1.0.0.json)
and immutable `v1.0.0` package remain unchanged.

GPIO4 and GPIO20 have independent finite conducted TONE evidence at
14,097,100 Hz, 2 mA and nominal 200 MHz `pll_sys`: seven cases per route,
including completion, four STOP timings, owner close and service stop.
Evidence retains its source/build, physical-path, receiver, cleanup and integrity
references. Attribution requires the exact released package/source, UAPIs,
runtime inventory, loaded module build notes and active route to match.
A development provider does not inherit release evidence from a version string.

The maintained [application band table](https://wsprry-pi.readthedocs.io/en/devel/About_Wsprry_Pi/index.html#band-qualification)
is the application qualification authority, maintained and explicitly accepted
by Lee Bussy, who holds the evidence provenance. The finite TONE cases do not
establish every band, mode, board, kernel or RF setup. Structural
`Compatible-unqualified` remains the module's resource state. Bundled evidence
neither changes application band/mode policy nor grants transmission permission.

## Installation and offline verification

The normal application installer installs the helper, reference and complete
bundle under `/usr/local/lib/wsprrypi`, including updates that skip an unchanged
provider. One application-owned receipt covers all six installed files.
Legacy helper/reference receipts can be upgraded or removed; modified or foreign
files are retained and reported. An interrupted upgrade can be retried.
No separate evidence-cache refresh is required. The old network-populated cache
is not used.

To verify the checkout's bundle and optionally export an independent copy,
without installing or inspecting a provider:

```sh
python3 -B scripts/rp1_qualification.py resolve
python3 -B scripts/rp1_qualification.py resolve --directory /absolute/evidence-copy
```

The legacy `refresh` command is also an offline verification/export alias.
Neither command accesses GitHub. Status reads the installed sibling bundle;
a missing or corrupt installed file remains an installation-integrity problem,
not a new application band restriction.

## Status and attribution

The installed command reads the verified local bundle and performs bounded
passive provider inspection. Target inspection requires separate authorization:

```sh
python3 /usr/local/lib/wsprrypi/rp1_qualification.py status --route gpio4
```

For offline review, supply `--provider-report` and `--ownership` JSON files;
`--directory` can select an explicit evidence input. Route evidence, structural
compatibility, operational readiness and current output remain independent.
An unknown or mismatched provider is reported as unattributed; local evidence
availability never promotes it to qualified or ready.

Ordinary scheduler status does not perform this inspection while holding
configuration or route locks. The diagnostic executable frozen in provider
`1.0.0-1` does not interpret this metadata; use the maintained application helper
for scoped attribution.
