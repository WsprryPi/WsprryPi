<!-- SPDX-License-Identifier: MIT -->

# RP1 qualification evidence and status

WsprryPi keeps installed provider identity, structural resource compatibility,
route readiness, published qualification evidence, application eligibility and
current output/ownership/cleanup state separate. The RP1 module continues to
report `Compatible-unqualified` when its structural resource contract passes.
Externally established qualification does not rewrite that module state.

The original provider [release lock](../scripts/tests/fixtures/rp1-release-1.0.0.json)
and all seven immutable `v1.0.0` assets remain unchanged. In particular the
original compatibility manifest is empty. A separate
`qualification-v1.0.0-r1` metadata publication records accepted evidence for
software `1.0.0` / package `1.0.0-1`. The supplemental consumer reference pins
its source, revision, complete four-asset inventory and every SHA-256.
The production semantic-version module selector excludes supplemental tags.

GPIO4 and GPIO20 have independent finite conducted TONE evidence at
14,097,100 Hz, 2 mA and nominal 200 MHz `pll_sys`: seven passing cases per
route, including completion, four STOP timings, owner close and service stop.
The evidence retains its original source/build, configuration, physical path,
receiver, cleanup and integrity references. Reuse for published `1.0.0` requires
unchanged executable/build inputs and equal extracted candidate/release package
members; their archive and source hashes remain different.

The maintained [application band table](https://wsprry-pi.readthedocs.io/en/devel/About_Wsprry_Pi/index.html#band-qualification)
is the accepted application qualification statement, maintained and explicitly
accepted by Lee Bussy, who holds the evidence provenance. Its Pi 5 / 200 MHz PLL_SYS column
qualifies 2200 m through 10 m, including 12 m; 6 m and 4 m are unqualified;
2 m, 1.25 m and 70 cm are unavailable. Application frequency construction and
band/mode policy remain in `src/WSPR-Transmitter`. An experimental override
cannot enable an Unavailable combination. Supplemental metadata contains no
provider admission or enrollment requirement and cannot enable a transmission.

## Resolution and runtime status

The application installer installs the maintained consumer interpreter and its
separate reference, then attempts to refresh the pinned evidence cache. A failed
refresh is reported without changing provider installation or application policy.
Status never downloads metadata. Missing evidence reports `Evidence missing`;
corrupt, substituted or stale bytes report `Evidence invalid`. Existing application
qualification remains independent in both cases.

To independently resolve and validate the supplemental publication without any
installation, service, endpoint or target effects:

```sh
python3 -B scripts/rp1_qualification.py resolve \
  --directory /absolute/external/supplement-download
```

The resolver verifies the exact immutable release, metadata tag/source,
original software tag/source, authoritative asset URLs, GitHub asset digests,
complete asset set and pinned bytes before executing the reviewed supplemental
interpreter. The schema and checksum assets are pinned as well. Changes require
an intentional supplemental reference update; no latest-metadata lookup occurs.

The installed command below reads the verified cache and performs the provider's
bounded passive inspection. Running it on a target requires separate target
authorization, including its passive endpoint inspection:

```sh
python3 /usr/local/lib/wsprrypi/rp1_qualification.py status --route gpio4
```

For offline review, supply captured `--provider-report` and `--ownership` JSON
files and an external `--directory`. Exact released package/source, both UAPIs,
active route, runtime overlay, installed artifact inventory and both loaded module
build notes must match before evidence is attributed. Tested board/kernel values
are observations, not new hardware or kernel permission lists. Scope is always
shown alongside qualification; it is not an all-band or all-mode claim.

Detailed route status responses expose `qualificationEvidence`, `liveQualification` and
`structuralCompatibility` independently of `operationalReady`, output inhibition,
eligibility and route transactions. The retained detailed UI uses scoped route
evidence and an independent application-eligibility label. Its compact route
workflow and transmission controls keep their existing behavior. The old diagnostic
executable inside frozen `1.0.0-1` has not been updated; use this consumer or
the separately identified supplemental interpreter for qualification status.

Ordinary scheduler status does not perform supplemental inspection while holding
configuration or route locks. Its generic `rp1_eligibility` is `not-evaluated`
until an application plan is assessed; a route's observational `eligible=false`
does not mean an application band/mode is unavailable. Administrative responses
that have not collected evidence explicitly report `Not inspected`.

## Operator documentation follow-up

`WsprryPi-Docs/docs/Advanced_Operations/rp1_gpclk.md` should explain the separate
route-evidence and readiness facts, cache verification/missing-evidence wording,
new diagnostic command and unchanged module diagnostic limitations. Preserve
`docs/About_Wsprry_Pi/index.md` band dispositions and scope. That separate
repository requires its own authorization and rendered documentation review.
