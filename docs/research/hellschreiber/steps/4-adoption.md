# Step 4 — Measure real adoption

## Status

Complete for public documentary evidence available from 2021-01-01 through 2026-07-29. The evidence establishes continuing organized amateur use, but it does **not** support a representative worldwide mode-share ranking. FSK Hell-105 has the strongest explicit recurring-net evidence; standard Feld-Hell has the strongest generic club identity and qualitative prevalence claim. Neither can be called the most-used on-air waveform from the available observations.

No callsign-level dataset was retained, no restricted group content was accessed, and no application, hardware, signal-generation, or RF work was performed.

## Decision summary

- **What can be said:** Hellschreiber remains an active amateur method. Public Feld Hell Club summaries document recurring sprints throughout the primary window, including entrant counts in multiple events. Three North American weekly nets explicitly specify FSKH105, while a separate European weekly net permits FSK/FM Hell-105 or original Feld-Hell and recommends FSK Hell-105 in fldigi. [HELL-EVID-0029] [HELL-EVID-0031] [HELL-EVID-0032]
- **What cannot be said:** No public source found supplies a representative denominator of mode-identified Hell contacts, stations, or receive observations. The sprint summaries generally say `Feld Hell` or `Hell`, and their public logs are incomplete or held in a member group. Schedule entries prove opportunity, not attendance. Therefore no worldwide observed-use leader passes the Step 1.2 evidence gate. [HELL-EVID-0014] [HELL-EVID-0015] [HELL-EVID-0029]
- **Narrow operational lead:** FSK Hell-105 is the best-supported candidate for Step 5 cross-software experiments because it is named by three current North American nets and recommended by the independent European net. This is a test-priority decision, not an adoption victory or implementation choice. [HELL-EVID-0031] [HELL-EVID-0032]
- **Software context:** fldigi dominates the club's year-end respondent samples—72% in 2022, 17 of 20 in 2023, and 17 of 24 reported software choices in 2024—but those surveys measure software, not Hell submode. [HELL-EVID-0030]

## Research question

Which Hellschreiber modes are in meaningful current amateur use, and can any mode defensibly be identified as the most common for amateur operation?

“Adoption” was decomposed into five claims that must not be substituted for one another:

1. **software-supported** — a maintained application can transmit or receive the profile;
2. **scheduled** — a net or event advertises an opportunity to use it;
3. **submitted** — an operator submitted a log or result to an organizer;
4. **observed** — a mode-identified transmission was received or a contact was recorded;
5. **representative prevalence** — observations have a defined population, denominator, identification method, and known enough bias to support a comparative share.

## Scope

The primary observation window is 2021-01-01 through 2026-07-29. Older material was used only to interpret labels and methods. The population of interest is worldwide amateur-radio Hellschreiber operation, not software installations, club membership, historical equipment, or Wsprry Pi suitability.

The candidates are the qualified profiles frozen in Step 2. Generic `Hell`, `Feld Hell`, and `FSK Hell` labels were not silently converted into exact waveform profiles.

## Method

### Mode-identification grades

| Grade | Meaning | Example in this step |
| --- | --- | --- |
| M1 | Explicit profile named in an observation or operating rule | `FSKH105 mode` on the North American net page |
| M2 | Exact profile follows unambiguously from a rule set | None recovered at population scale |
| M3 | Profile inferred from software/default behavior | fldigi recommendation; retained only as inference |
| M4 | Generic family label | `Feld Hell`, `Hell`, or club sprint identity |
| M5 | Ambiguous or conflicting label | ADIF or prose `FSKHELL` without rate/profile |

Only M1 and M2 may support an exact-profile count. M3 is software evidence, and M4/M5 remain family-level evidence.

### Source-quality disposition

| Disposition | Requirement | Sources in this step |
| --- | --- | --- |
| Usable quantitative | Defined unit, time window, denominator or exact count, and interpretable selection process | Club year-end respondent software counts, individual sprint entrant counts |
| Quantitative with limitations | Exact count but selected population, missing fields, or incomplete coverage | Public sprint summaries and net schedule counts |
| Qualitative | Maintainer/organizer description without a comparative denominator | Club FAQ prevalence wording, software documentation |
| Rejected for ranking | Availability, vocabulary, search hits, membership, or schedule without observations | ADIF enumeration, software menus, ARRL calendar entries, club membership totals |

### Inclusion and privacy

Only public organizer pages, official software documentation, and official interchange specifications were used. Full sprint entries referenced as residing in a restricted Groups.io community were not accessed. Callsigns appearing incidentally on public pages were neither transcribed into the report nor retained in an artifact. No public user-level dataset was necessary for the conclusions.

## Source inventory and findings

### Feld Hell Club sprint summaries: continuing activity, weak submode identity

The club publishes annual sprint-result summaries for 2021 through 2025. These establish repeated organized activity and sometimes exact entrant counts, but most events are labeled only as Feld Hell or Hell. Public summaries also state that complete entry lists for recent years are on Groups.io. They do not consistently expose a submode field. [HELL-EVID-0029]

The explicit numeric statements give conservative lower bounds of **169 entrant-event submissions in 2021**, **83 in 2022**, **84 in 2023**, and **72 in 2024**. These are sums of only the event counts stated in public prose; events without a number are omitted. They are not unique operators, contacts, or mode-coded observations. The lower bounds therefore show continuity and scale inside one self-selected club program, not worldwide adoption share.

The 2025 page lists 15 sprints and records no entries for one event, but supplies no public entrant count for the others. It therefore supports continued scheduling and at least an organizer-maintained result series, not a 2025 participation total.

### Club software surveys: fldigi leads a selected respondent group

The club's December summaries report:

| Survey | Reported distribution | Denominator | Interpretation |
| --- | --- | ---: | --- |
| 2022 year-end sprint | fldigi 72%, Ham Radio Deluxe 17%, MultiPSK 11% | Not stated directly; percentages imply a small respondent set | Quantitative with limitations |
| 2023 year-end sprint | fldigi 17, Ham Radio Deluxe 3 | 20 entrants/respondents | Usable for this selected event sample |
| 2024 year-end sprint | fldigi 17, Ham Radio Deluxe 4, MultiPSK 1, MixW 1, unknown 1 | 24 reported choices; event had 25 entries | Usable with one apparent nonresponse or count mismatch |

Across all three samples fldigi is the clear software leader. The samples are annual sprint entrants rather than a random operator population, and software choice does not identify which of fldigi's seven Hell profiles was used. This evidence cannot be converted into Feld-Hell or FSKH105 mode share. [HELL-EVID-0030]

### North American nets: strongest exact-profile schedule evidence

The Feld Hell Club net page, updated 2025-04-10, lists three weekly North American nets—30 m Wednesday, 40 m Thursday, and 80 m Friday—and marks all three `FSKH105 mode`. This yields three explicit recurring schedule series at M1. The page gives operating instructions and net-control details but no attendance, check-in, or contact totals for the research window. [HELL-EVID-0031]

This is the strongest exact-profile evidence found, but it is **scheduled use**, not observed share. Counting each weekly recurrence as an observation would multiply organizer intent rather than measure transmissions.

### European net: mixed rule with an FSKH105 recommendation

The independently maintained European Feld Hell Net schedules one weekly Saturday opportunity. Its current rule permits `FSK Hell-105/FM-Hell-105 or original FeldHell`; its software instructions recommend fldigi set to `FSK Hell-105`. The net explicitly states that it has no check-in list and no coordinating station. [HELL-EVID-0032]

The operating rule is M1 for a mixed allowed set, not M1 evidence that each session used FSKH105. The recommendation makes FSKH105 the easiest reproducible setup to test, but not an observed winner.

### Club FAQ and fldigi documentation: prevalence claims without samples

The Feld Hell Club FAQ calls single-tone Feld-Hell the most popular Hellschreiber mode. fldigi describes Feld-Hell as the traditional method and exposes it alongside six other profiles. These are informed ecosystem descriptions, but neither supplies dates, observations, a denominator, or a sampling method for the prevalence claim. They are retained as qualitative corroboration only. [HELL-EVID-0033] [HELL-EVID-0011]

### Logging and reporting systems: vocabulary is not a dataset

ADIF recognizes `FMHELL`, `FSKH105`, `FSKH245`, `FSKHELL`, `HELL80`, `HELLX5`, `HELLX9`, `HFSK`, `PSKHELL`, and `SLOWHELL` under `HELL`. This enables mode-coded logging but does not prove consistent exporter behavior or provide public observations. Earlier Step 1.2 evidence also records export-name discrepancies. [HELL-EVID-0014]

No public, queryable, representative receive-report corpus with a reliable Hell submode field was found. Search results, Hamspots availability, software download counts, and ARRL calendar listings were rejected as mode-share denominators.

## Adoption matrix

| Candidate/profile | Software support | Explicit current schedule | Submitted/observed public data | Identification quality | Step 4 disposition |
| --- | --- | --- | --- | --- | --- |
| Standard Feld-Hell | Broad in Step 2 sources | Permitted by European weekly net; generic Feld Hell club events | Club activity is largely M4; no exact-profile denominator | M4 generally; M1 only as an allowed option | Strong family identity; qualitative prevalence lead only |
| FSK Hell-105 / FM Hell-105 qualified profile | fldigi and other documented programs | Three weekly NA nets; permitted and recommended by weekly EU net | No attendance or representative contact count | M1 schedule, not M1 observation | **Strongest explicit schedule footprint; Step 5 priority** |
| FSK Hell-245 | Supported by fldigi and other documented programs | None found in primary window | None found | No adoption-grade observation | Supported, adoption unknown |
| Slow Hell (fldigi profile) | Supported by fldigi | None found | None found | No adoption-grade observation | Supported, adoption unknown |
| Feld X5 / X9 | Supported by fldigi | None found | ADIF can encode them; no public counts found | Vocabulary only | Supported, adoption unknown |
| Hell-80 | Supported by fldigi/MultiPSK profiles | None found | ADIF can encode it; no public counts found | Vocabulary only | Niche/unknown; historical-label ambiguity remains |
| PSK/FM/MSK/Duplo/S-MT/C-MT families | Some documented software/history | No recurring schedule with unambiguous profile found | None representative | Mostly taxonomy/software evidence | Adoption unknown or not measurable |
| G3PPT Slow-Feld beacon profiles | Historical/documented beacon method | No current primary-window schedule found | None found | No adoption-grade observation | Current adoption not established |

## Sensitivity analyses

### If schedules are counted as adoption

FSKH105 becomes the apparent leader because it has three explicitly named North American weekly nets and is the recommended setup for the European net. This result is highly sensitive to treating a scheduled opportunity as if it were an attended transmission. That substitution is rejected for a prevalence claim, but accepted for choosing an offline test priority.

### If generic Feld Hell club events are assigned to standard Feld-Hell

Standard Feld-Hell becomes the apparent leader because the club runs many sprints and calls Feld-Hell its most popular mode. This requires converting M4 family labels into an exact waveform. The club's own 2023 advice to use TxID “to identify in what mode” indicates that more than one profile may appear. The conversion is therefore rejected.

### If software choice is assigned to its default or namesake mode

fldigi's large respondent share would dominate and could be mapped to whichever Hell profile is assumed to be default. Because fldigi exposes seven profiles and the survey did not ask for submode, the mapping is unsupported. The conclusion changes with an arbitrary default assumption and is rejected.

### If only exact mode-identified observations are admitted

The usable comparative dataset is empty: the net records are schedules, not check-ins, and the public sprint summaries are not consistently submode-coded. This is the controlling sensitivity case for a worldwide most-used claim.

## Bias and concentration

- **Selection bias:** Sprint submitters are contest-oriented club participants, not all Hell operators.
- **Geographic bias:** Public net evidence is concentrated in North America and Europe.
- **Activity bias:** Nets and sprints overrepresent organized operation and underrepresent casual QSOs, experiments, and unattended beacons.
- **Identification bias:** Generic `Hell` and `Feld Hell` labels conceal the exact waveform; software can support several profiles.
- **Survivorship/publication bias:** Events with published summaries are visible; casual or unsuccessful activity is not.
- **Repeat-participant concentration:** Entrant-event totals can count the same operator repeatedly. Public summaries do not provide a privacy-preserving unique-operator series suitable for concentration analysis.
- **Platform bias:** fldigi's survey lead may reflect club instructions, operating-system reach, logging convenience, or TxID support rather than waveform preference.

Because there is no representative mode-coded observation set, formal mode shares, confidence intervals, HHI concentration, geographic shares, or trend regression would create false precision and were not calculated.

## Conclusions and confidence

| Conclusion | Evidence state | Confidence |
| --- | --- | --- |
| Organized amateur Hellschreiber activity continued throughout 2021–2025 and current net pages remain maintained in 2026 | `OBS` from organizer pages | Moderate–high for organized activity; not representative prevalence |
| fldigi is the most common software in the three available Feld Hell Club year-end respondent samples | `OBS` | High for those samples; low for worldwide operators |
| FSKH105 has the strongest explicit recurring-net schedule evidence found | `OBS` | High for the examined public schedules |
| Standard Feld-Hell has the strongest generic club identity and qualitative popularity claim | `OBS` plus organizer judgment | Moderate as ecosystem description; low as measured prevalence |
| A worldwide most-used Hellschreiber waveform can be selected from public data | `UNKNOWN` / unsupported | High confidence that the current evidence is insufficient |

The requested “best or most common for ham” conclusion must therefore be split:

- **Most common measured on air:** not established.
- **Most explicit organized operating target:** FSK Hell-105, provisionally, for offline interoperability planning.
- **Most established reference and generic club mode:** standard Feld-Hell, without a quantified share.
- **Best for Wsprry Pi:** not decided; Steps 5 and 6 remain mandatory.

## Decisions affected

- HELL-DEC-0010 is satisfied: supported, scheduled, submitted, and observed use remain separate.
- HELL-DEC-0018 is resolved only as an adoption non-ranking; no production mode is selected.
- HELL-DEC-0023 records that no worldwide observed-use leader is established.
- HELL-DEC-0024 gives FSKH105 first priority in Step 5, followed by standard Feld-Hell, because schedule evidence makes that pairing operationally relevant.

## Unresolved questions

- Can organizers provide privacy-preserving, submode-coded aggregate check-in or contact totals for the primary window?
- Do net controllers retain aggregate attendance by session and exact profile without requiring callsign-level storage?
- Can public ADIF collections be identified with a defined population and validated exporter mappings?
- How often do North American FSKH105 nets actually convene, and how many check-ins occur?
- What share of European net activity uses FSKH105 versus original Feld-Hell?
- Does casual or beacon use materially favor modes not represented by club events?

## Artifacts

No dataset was committed. The source pages are linked in the evidence register, the reported counts are small enough to audit directly from their prose, and retaining copied callsign-level or restricted data was unnecessary.

## Recommended next step

Proceed to Step 5 with two initial offline interoperability tracks:

1. FSK Hell-105/FM Hell-105 as the strongest exact-profile organized-use candidate;
2. standard Feld-Hell as the historical reference and strongest generic amateur identity.

Add other profiles only after those fixtures and cross-decoder tests are stable. This ordering is an experiment-priority decision, not implementation approval.
