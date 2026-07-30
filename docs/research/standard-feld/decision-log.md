# Standard Feld decision log

Decision identifiers in this record are independent of the completed generic Hellschreiber review.

| ID | Date | Phase | Status | Decision | Basis | Revisit condition |
| --- | --- | --- | --- | --- | --- | --- |
| SF-DEC-0001 | 2026-07-30 | 1.1 | Accepted | Use `standard-feld-wsprry-v1-draft` as the unambiguous proposed profile identifier until Gate A and Gate B close. | HELL-DEC-0036, HELL-DEC-0042; SF-EVID-0001 | Gate A/B approval assigns the non-draft identifier or a reviewed replacement |
| SF-DEC-0002 | 2026-07-30 | 1.1 | Accepted | Make 245 physical positions/s, not a rounded millisecond interval, the normative clock; retain 122.5 baud only as conventional nomenclature. | SF-EVID-0002, SF-EVID-0003 | Material primary evidence invalidates the established timing model |
| SF-DEC-0003 | 2026-07-30 | 1.1 | Accepted | Traverse transmitted columns left to right and the 14 positions within each column bottom to top. | SF-EVID-0002 | Material primary evidence invalidates scan order |
| SF-DEC-0004 | 2026-07-30 | 1.1 | Accepted | Preserve arbitrary half-position feature placement; do not silently reduce the profile to fixed identical position pairs. | SF-EVID-0003 | A separately named paired profile is proposed and independently qualified |
| SF-DEC-0005 | 2026-07-30 | 1.1 | Accepted | Define logical `1` as RF enabled and `0` as RF disabled at one carrier; safe initial, idle, cancellation, completion, and fault state is RF disabled. | SF-EVID-0001, SF-EVID-0004 | Later safety review strengthens but does not weaken the RF-disabled terminal requirement |
| SF-DEC-0006 | 2026-07-30 | 1.1 | Accepted | Derive all offsets and durations from integer physical-position counts and rational arithmetic. | SF-EVID-0004, SF-EVID-0005 | A reviewed exact representation proves equivalent determinism without cumulative error |
| SF-DEC-0007 | 2026-07-30 | 1.1 | Accepted | Keep normalization distinct from substitution and forbid silent input transformations before the asset contract defines them. | SF-EVID-0001, SF-EVID-0003 | Gate B approves explicit versioned transformations |
| SF-DEC-0008 | 2026-07-30 | 1.1 | Accepted | Retain the current repeat rule: compiled duration equal to the repeat interval is valid. | SF-EVID-0004 | Architecture review finds a documented safety or scheduling conflict |
| SF-DEC-0009 | 2026-07-30 | 1.1 | Accepted | Define every physical-position boundary as a protocol cancellation boundary; treat measured backend latency as a later qualification result. | SF-EVID-0004 | Backend design requires a stricter operator contract while preserving safe stop |
| SF-DEC-0010 | 2026-07-30 | 1.1 | Deferred | Defer repertoire, bitmaps, artwork placement within fixed seven-column cells, word spacing, normalization, substitution, leader/trailer counts, and glyph-bearing fixtures to Gate B and Phase 1.2. | SF-EVID-0001 through SF-EVID-0003 | One immutable redistributable asset and spacing policy are approved |
| SF-DEC-0011 | 2026-07-30 | 1.1 | Rejected | Do not inherit a font or spacing behavior merely from fldigi, xfhell, RadioLib, or a historical transcription. | SF-EVID-0001 through SF-EVID-0003 | Never for silent inheritance; an explicit reviewed selection remains allowed |
| SF-DEC-0012 | 2026-07-30 | 1.1 | Accepted | Mark Phase 1.1 passed but keep Gate A open until exact asset-bound fixtures exist. | HELL-DEC-0037; SF-EVID-0001 | Gate A evidence is completed or a blocking contradiction is found |
