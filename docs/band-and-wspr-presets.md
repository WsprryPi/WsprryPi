# Canonical Band Correlation and WSPR Preset Contract

## Purpose

WsprryPi needs one application-wide band correlation model for WSPR, Test Tone,
QRSS, FSKCW, and DFCW. The correlated band selects configured Band GPIO/filter
behavior and associates a request with backend/profile/mode qualification.

Qualification is a project capability record, not a determination of an
operator's legal authority. A backend that is not qualified for a correlated
band and mode may be blocked. The transmitter enforces qualification state,
experimental overrides, immutable policy, representability, and hardware-safety
checks.

Canonical WSPR names and dial frequencies are a separate WSPR-only convenience
layer. They must not control the meaning of a band for CW modes.

## Ownership and boundaries

A neutral `src/Band-Lookup` component owns the single frequency-to-band
correlation catalog because the catalog serves every mode, Band GPIO/filter
selection, and final transmitter policy. The parent `BandLookup` facade and
`src/WSPR-Transmitter` consume that same definition. Neither consumer may
retain a second frequency-edge table.

The transmitter remains authoritative for final numeric, backend,
representability, lifecycle, cancellation, output-inhibition, and cleanup
checks. Frequencies outside the correlation catalog must not infer a nearest
band or filter; experimental controls and explicit selector rules apply.

Qualification state is keyed by correlated band, backend, hardware profile,
and mode. A band-level classification does not claim that the entire
correlation envelope has been measured.

## Correlation catalog

The ordinary worldwide amateur correlation scope is 2200 m through 70 cm. A
band allocated to the ordinary amateur service in at least one jurisdiction is
in scope even when it is not an ITU-wide allocation. Experimental,
trial, ISM, Part 15, special-research, and individually granted extension
frequencies are excluded. The 22 m entry is not part of the canonical amateur
catalog.

| Canonical band | Correlation envelope |
| --- | ---: |
| `2200m` | 130-190 kHz |
| `630m` | 472-479 kHz |
| `160m` | 1.8-2.0 MHz |
| `80m` | 3.5-4.0 MHz |
| `60m` | 5.25-5.45 MHz |
| `40m` | 7.0-7.3 MHz |
| `30m` | 10.1-10.15 MHz |
| `20m` | 14.0-14.35 MHz |
| `17m` | 18.068-18.168 MHz |
| `15m` | 21.0-21.45 MHz |
| `12m` | 24.89-24.99 MHz |
| `10m` | 28.0-29.7 MHz |
| `8m` | 40-45 MHz |
| `6m` | 50-54 MHz |
| `5m` | 54-68 MHz |
| `4m` | 69.9-70.5 MHz |
| `2m` | 144-148 MHz |
| `1.25m` | 219-225 MHz |
| `70cm` | 420-450 MHz |

These are correlation envelopes. They can contain jurisdiction-specific gaps
and do not assert operating authority. Because a numeric frequency can have
only one correlation identity, the exact shared 54 MHz edge correlates to `6m`;
`5m` begins at the next integral hertz.

## Canonical band identity

Canonical persisted and API names are lowercase unit-bearing identifiers from
the table above. Matching may be case-insensitive. `lf` and `mf` may remain
accepted compatibility aliases for `2200m` and `630m`, but output should use the
canonical name. Display formatting such as `1.25 m` is presentation, not
identity.

## WSPR preset identity

A band has one correlation identity but may have multiple WSPR frequency
presets. A common convention can use the bare band name, while a convention that
needs disambiguation uses a qualified WSPR-only preset identifier. The first
implemented qualified identities are:

```text
60m:legacy = 5,287,200 Hz USB dial
60m:wrc15  = 5,364,700 Hz USB dial
```

All of these correlate to the same `60m` band. Qualified preset names do not
create new bands and do not apply to QRSS, FSKCW, or DFCW.

Existing installations and configurations must retain the current meaning of a
bare `60m` unless the operator selects another WSPR frequency profile or local
preference. The current convention should also receive an explicit stable name,
such as `60m:legacy`.

The current WSPRnet frequency list publishes both 60 m dial frequencies, while
the retained WSPRnet QRG reference identifies 5,287.2 kHz as the established
60 m dial frequency. The qualified names describe those WSPR conventions; they
do not claim that either frequency is authorized in a particular jurisdiction.
Do not add country labels such as `:us` or `:uk` without a maintained source and
a demonstrated operator-convenience need.

## WSPR frequency profiles and local preferences

The parent application provides an explicitly selected WSPR frequency profile,
persisted as `WSPR.Frequency Profile`. The compatibility profile is
`existing_common`; the alternate profile is `wrc15`. Maintained country or
locality profiles may choose a different default preset for a band.
IARU regions alone are insufficient where national conventions differ.

The application must not infer locality from an IP address or callsign. A
profile is an operator-selected convenience default, not a regulatory claim.

Local per-band preferences belong in the normal parent WsprryPi configuration
lifecycle, alongside the selected WSPR profile. They do not belong in the
`src/WSPR-Transmitter` component. They are stored as the JSON object
`WSPR.Band Preferences`; the INI representation is the same object serialized
on one line. For example:

```ini
[WSPR]
Frequency Profile = existing_common

Band Preferences = {"60m":"60m:wrc15"}
```

Preference keys are canonical bands. Values are either built-in preset
identifiers that correlate back to the same band or positive integral USB dial
frequencies that correlate to that band. A 60 m preset preference must be
qualified, so changing the selected profile cannot change its meaning. Numeric
preferences provide the implemented local-frequency mechanism; they do not
create new named presets.

The UI exposes all 19 bands from 2200 m through 70 cm. Each row can follow the
profile/default, select an available named preset, or use a custom integral USB
dial frequency. It previews the effective dial and RF tone, validates before
autosave, clears by removing only that band's preference, and preserves the
typed preset-string or numeric representation across round trips.

WSPR frequency resolution precedence is:

1. explicit numeric frequency;
2. explicit qualified preset such as `60m:wrc15`;
3. local per-band preference;
4. selected WSPR frequency profile;
5. built-in Existing/Common preset.

Changing profiles must not rewrite explicit numeric entries, explicit qualified
presets, or saved local preferences.

The implemented profile resolver currently changes only bare `60m`:

| Profile | Effective bare `60m` preset |
| --- | --- |
| `existing_common` | `60m:legacy` (5,287,200 Hz USB dial) |
| `wrc15` | `60m:wrc15` (5,364,700 Hz USB dial) |

All other bare bands retain their existing/common preset under both profiles.
The selected profile is returned with the effective compatibility band catalog
so Test Tone and scheduled WSPR planning resolve the same dial frequency.
Configured numeric preferences also add `8m` and `5m` to that effective catalog;
those bands have correlation envelopes but no built-in WSPR preset.

## Configuration compatibility

- Preserve current bare aliases and dial frequencies by default.
- Keep explicit names for established conventions when changing profile behavior.
- Remove `22m` from the canonical amateur and WSPR catalogs with an explicit
  compatibility diagnostic; do not silently remap it.
- Keep a one-effective-preset-per-band response for compatibility while adding
  a separate complete preset catalog capable of returning multiple presets per
  band.
- Include both canonical band and WSPR preset identity in new control-plane
  responses and diagnostics.
- Existing valid preset-only and numeric preference objects remain valid.
- A missing or empty `Band Preferences` object preserves the default behavior.

## Frequency references

- WSPRnet, current "Frequencies" list:
  <https://www.wsprnet.org/drupal/WSPRnet/map>
- WSPRnet, "WSPR Frequencies" retained QRG reference:
  <https://www.wsprnet.org/drupal/sites/wsprnet.org/files/wspr-qrg.pdf>
- IARU Region 1, WRC-15 5 MHz allocation context:
  <https://www.iaru-r1.org/about-us/committees-and-working-groups/hf-committee-c4/news/cept-licenses-do-not-allow-operation-at-60-meters-in-portugal/>
