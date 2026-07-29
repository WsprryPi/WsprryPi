# Step 2 — Build the mode taxonomy

## Status

Complete as a documentary taxonomy. This step classifies evidenced Hellschreiber protocols, families, profiles, aliases, apparatus names, and unresolved labels. It does not rank amateur adoption, freeze a font, demonstrate interoperability, select a Wsprry Pi mode, or authorize implementation, hardware operation, or RF transmission.

## Answer first

“Hellschreiber” is a family of visibly decoded raster telegraph systems, not a single protocol and not a simple list of baud-rate variants. The defensible taxonomy has three signaling branches:

1. **Sequential on/off raster:** Presse/F-Hell, standard Feld-Hell, GL-Hell, and compatible Feld speed profiles.
2. **Continuous two-frequency or phase:** FSK/FM/MSK Hell, PSK-Hell, Hell-80, and Duplo-Hell.
3. **Frequency-addressed rows:** sequential multi-tone (S/MT), concurrent multi-tone (C/MT), and Chirp Hell.

Within those branches, the taxonomy must distinguish a protocol family from a profile. Feld X5, Feld X9, and fldigi Slow Hell preserve the Feld raster method while changing rate; they are profiles, not new signaling families. FSK-Hell is an under-specified family name, not one universal waveform. PSK-Hell and FM/MSK-Hell are not synonyms because their transmitted phase/frequency behavior differs even where a common differential-phase receiver can display both. Historical Siemens Hell-80 and fldigi's Hell-80 profile are also not bit-exact equivalents: the former is a 9-by-7, 315-baud format while the latter reuses fldigi's common 14-sample column machinery at 245 samples/s. [HELL-EVID-0017] [HELL-EVID-0018] [HELL-EVID-0021]

No “best” or “most common for ham” conclusion is made in Step 2. Software support and scheduled operation are retained as leads for Step 4, not prevalence data.

## Research question

Which named Hellschreiber methods represent distinct on-air protocols, which are compatible profiles or implementations, which names are aliases or apparatus/service labels, and which candidates remain too poorly specified to classify?

## Scope and method

The Step 1.1 Feld-Hell contract and Step 1.2 comparison framework remain controlling. Candidate names were traced through original manuals where available, inventor/developer descriptions, technical reconstructions tied to historical equipment, current official software documentation, and immutable source. A name was promoted to a distinct family only when evidence established a material difference in raster traversal, framing/synchronization, state-to-waveform mapping, or receiver assumption.

The classification levels are:

- **Family:** a materially distinct signaling method or receive model.
- **Protocol:** a sufficiently specified on-air contract within a family.
- **Profile:** parameter choices or a compatible rate/font variant within a protocol or family.
- **Implementation profile:** a program's specific approximation or mapping; not assumed universal.
- **Alias:** a second name for the same evidenced contract, qualified by context where necessary.
- **Apparatus/service label:** equipment or use whose on-air protocol is inherited from another entry.
- **Unresolved candidate:** a reported name lacking enough evidence for promotion.

## Canonical family tree

```text
Hellschreiber
├── Sequential on/off raster
│   ├── Presse-Hell / F-Hell historical service family
│   │   └── 245-baud, 5-character/s high-speed profile
│   ├── Feld-Hell protocol
│   │   ├── Standard Feld-Hell (Step 1.1 reference)
│   │   ├── fldigi Slow Hell (1/8-rate profile)
│   │   ├── Feld X5 (5x-rate profile)
│   │   ├── Feld X9 (9x-rate profile)
│   │   └── Slow-Feld beacon profiles (about 2 characters/minute)
│   └── GL-Hell / Hell-72 start-stop protocol
├── Continuous two-frequency or phase raster
│   ├── FSK/FM/MSK-Hell family
│   │   ├── FSK Hell-245 profile
│   │   ├── FM/MSK Hell-105 profile
│   │   └── FM/MSK Hell-245 profile
│   ├── PSK-Hell family
│   │   ├── PSK Hell-105 profile
│   │   └── PSK Hell-245 profile
│   ├── Hell-80 family
│   │   ├── Siemens Hell-80 historical protocol
│   │   └── fldigi Hell-80 implementation profile
│   └── Duplo-Hell protocol
└── Frequency-addressed-row raster
    ├── S/MT-Hell family
    ├── C/MT-Hell family
    └── Chirp Hell family
```

PC-Hell/Hell-45, HFSK, and generic “MFSK Hell” remain outside the canonical tree pending adequate definitions.

## Comparison matrix

Rates use source terminology unless explicitly normalized. A `?` marks a material unresolved parameter, not an implied default.

| Canonical entry | Class | Defining on-air characteristic | Raster/timing | Synchronization/framing | Alias and interoperability treatment | Confidence |
| --- | --- | --- | --- | --- | --- | --- |
| Presse-Hell / F-Hell | Historical family | Sequential current/OOK tone raster used for press service | Common high-speed profile: 14x7 transmitted field, 245 intervals/s, 5 char/s; equipment also supported slower service profiles | Quasi-synchronous continuous printing | `F-Hell` is the early press-format name; `Presse-Hell` can denote the broader service/equipment family, so qualify the rate | Moderate–high |
| Standard Feld-Hell | Reference protocol | Sequential OOK/ASK raster | Historical 7x14 physical raster at 245 positions/s with two-position minimum runs; conventional 122.5 baud, 2.5 char/s; fixed-pair 7x7 is a compatible simplification | Continuous asynchronous raster; no character start/stop; double printing at receiver | `Feldhell`, `Feld Hell`, `Feld-Hell`; corrected Step 1.1 contract governs | High |
| fldigi Slow Hell | Feld profile | Same sequential Feld mechanism at 1/8 column rate | 2.1875 columns/s; 14 samples/column; 30.625 samples/s; about 0.3125 char/s under seven-column timing | Same continuous Feld behavior | Not the same as G3PPT Slow-Feld beacon operation | High for fldigi |
| Feld X5 | Feld profile | Standard Feld mechanism at 5x rate | 87.5 columns/s; 1,225 samples/s in fldigi's paired representation; 12.5 char/s | Same continuous Feld behavior | ADIF `HELLX5`; speed compatibility requires a matching profile | High for fldigi |
| Feld X9 | Feld profile | Standard Feld mechanism at 9x rate | 157.5 columns/s; 2,205 samples/s in fldigi's paired representation; 22.5 char/s | Same continuous Feld behavior | ADIF `HELLX9`; speed compatibility requires a matching profile | High for fldigi |
| Slow-Feld beacon family | Feld-derived profile family | Extremely slow Feld-style raster for propagation beacons | Reported around 2 char/min; several versions | Continuous display; exact profile-dependent behavior unresolved | Must not be shortened to `Slow Hell` without a rate/profile qualifier | Moderate |
| GL-Hell / Hell-72 | Distinct protocol | Sequential raster with an embedded start indication and mechanical start-stop character cycle | Press-style 14x7 field; about 300 baud and 6.1 char/s; keyed tone pair reported | Asynchronous character start; no ordinary stop bit, mechanical cycle supplies spacing | T.typ.72 GL is apparatus; T.typ.73 AGL adds tape facilities but uses the GL protocol | Moderate–high |
| Generic FSK-Hell | Family label only | Raster state selects between two continuous tones | No universal rate, shift, font, or idle convention | Usually continuous; profile-dependent | Never infer `FSKH245` or `FMHELL` from this label alone | High that it is ambiguous |
| FSK Hell-245 | FSK-Hell profile | Continuous two-frequency raster; common implementation uses about 245 Hz rate and shift | Feld-style field; 245 intervals/s; about 2.5 char/s when paired representation is used | Continuous white/black tones; inter-character convention varies by implementation | `FSKH245` is a useful software profile label, not a universal FSK-Hell definition | Moderate–high |
| FM/MSK Hell-105 | FM/MSK profile | Continuous-phase two-frequency/MSK-style raster | 105 baud; special low-resolution 7x6/even-column font; about 2.5 char/s | Continuous phase/frequency; no character framing | `FM Hell-105`, `FSK Hell-105`, and fldigi's internal FM105 mapping are implementation-context aliases, not proof that generic FSK and FM mean the same thing | Moderate–high |
| FM/MSK Hell-245 | FM/MSK profile | Same continuous-phase family at the Feld paired-sample rate | 245 baud; common computer/Feld-style raster; about 2.5 char/s | Continuous phase/frequency | Keep distinct from PSK Hell-245 | Moderate |
| PSK Hell-105 | PSK profile | Differential phase transitions encode raster state, with shaped amplitude in the documented amateur design | 105 baud; special 7x6/even-column font; about 2.5 char/s | Continuous stream; differential phase receiver | A receiver may share processing with FM-Hell, but transmitted waveform is distinct | Moderate–high |
| PSK Hell-245 | PSK profile | Differential phase Hell at higher raster rate | 245 baud; 14x7 computer-style field | Continuous stream | Not an alias for FM/MSK Hell-245 | Moderate–high |
| Siemens Hell-80 | Distinct historical protocol | Two-tone FSK raster, black/white tones separated by 300 Hz | 9x7 = 63 elements, 315 baud, 5 char/s; reported 1625/1925 Hz tones | Original equipment supports synchronous and start-stop operation | `Hell-80` without implementation qualifier should mean this historical contract | High for core parameters |
| fldigi Hell-80 | Implementation profile | Continuous two-frequency display profile labeled Hell-80 | 35 columns/s x 14 samples/column = 490 samples/s internally; documentation labels 245 baud and 5 char/s; common fldigi font engine | No emulation of original start-stop framing | Human-readable similarity is plausible; bit-exact historical compatibility is unproven and must be tested | High for source behavior; low for historical equivalence |
| Duplo-Hell | Distinct protocol | Two raster columns sent concurrently on two OOK tones | Feld font/column throughput; 61.25 baud because each keyed unit lasts twice a Feld cell | Continuous raster | Not ordinary two-state FSK: tones represent simultaneous columns | Moderate |
| S/MT-Hell | Distinct family | One row at a time; row identity encoded by tone frequency while scanning up then across | Common 7x5 source field; rate and shortened white time are profile parameters | Sequential continuous raster | One-, five-, and seven-frequency reports should be profiles only after exact contracts are recovered | Moderate |
| C/MT-Hell | Distinct family | All marked rows in a column transmitted concurrently on row-specific tones | Row/tone count varies: reported 9, 14, or 16 and other profiles | Column clock; simultaneous tones within a column | Row-count names are profiles unless tests show incompatible framing beyond the expected tone map | Moderate |
| Chirp Hell | Distinct family | A column is represented by repeated frequency sweeps through row positions | Mostly experimental picture/test profiles; exact rate varies | Column-oriented sweeps | Related to C/MT and S/MT but neither an alias nor an ordinary FSK mode | Moderate |

## Why the important names are not interchangeable

### Slow Hell is not Slow-Feld

Current fldigi source makes `Slow Hell` exactly one eighth of its Feld column rate. Murray Greenman's historical/developer survey describes `Slow-Feld` as a G3PPT beacon technique around two characters per minute with several versions. Those rates differ by roughly an order of magnitude. The taxonomy therefore requires the qualified labels `fldigi Slow Hell` and `Slow-Feld beacon profile`; unqualified `Slow Hell` is unsafe in specifications. [HELL-EVID-0017] [HELL-EVID-0021]

### FSK-Hell is a family; FM/MSK and PSK describe waveforms

The developer record explicitly says FSK-Hell had no single standard and implementations differed in shift and behavior between characters. FM-Hell uses an MSK-like continuous-frequency waveform; PSK-Hell uses differential phase transitions with a shaped envelope. A common receiver concept does not make their transmitters identical. fldigi's display label `FSK Hell-105`, internal object name `FM105`, and RSID name `FM_HELL_105` demonstrate an implementation alias, not a universal naming rule. [HELL-EVID-0017] [HELL-EVID-0021]

### Hell-80 requires an implementation qualifier

The Siemens manual establishes an actual Model 80 apparatus with keyboard, punched-tape, synchronous, and start-stop operation. The historical contract is independently described as 9 rows by 7 columns, 315 baud, 5 char/s, and 300 Hz two-tone FSK. fldigi instead applies its generic 14-sample-column transmitter and selectable common fonts while its public mode table uses 245 baud. This is too large a structural difference to silently normalize. Until Step 5 tests actual decoding, the relationship is `F3 candidate/UNKNOWN`, not F1 or F2. [HELL-EVID-0018] [HELL-EVID-0020] [HELL-EVID-0021]

### Presse-Hell and F-Hell overlap but need a rate qualifier

The early developer taxonomy uses F-Hell/Press-Hell for the 245-baud, 5-char/s press format and says it is otherwise like Feld-Hell. Historical press equipment also offered 2.5-char/s operation, and some services used still other rates or fonts. `F-Hell 245/5` is therefore a specific profile; `Presse-Hell` alone is a broader historical service/equipment family. [HELL-EVID-0017] [HELL-EVID-0019]

## Apparatus and service names

| Name | Taxonomic treatment | Rationale |
| --- | --- | --- |
| T.typ.58 / Tbs 24a-32 | Standard Feld-Hell apparatus | Machine identity, not an additional protocol |
| T.typ.72 GL | GL-Hell apparatus | Origin of the start-stop GL protocol name |
| T.typ.73 AGL | GL-Hell apparatus profile | Tape-reader facilities do not by themselves establish a new on-air protocol |
| Siemens Hell-80 / T.typ.80 | Hell-80 apparatus and protocol | Its raster, rate, tones, and framing distinguish it |
| Thomson Hell machines | Apparatus profile, provisionally Feld-compatible | Historical reconstruction reports probable Siemens Feld compatibility; manufacturer attribution and exact variants remain incomplete |
| L-Hell | Receive/display apparatus label under GL-era equipment | Available evidence does not establish a separate transmitter protocol |
| Weather-Hell / News-Hell | Service/use label | Content or service purpose is not a waveform definition |

## Software and logging vocabulary crosswalk

| Label | What it safely establishes | What it does not establish |
| --- | --- | --- |
| fldigi menu entries | A current implementation profile with inspectable timing and waveform logic | Historical norm or relative amateur popularity |
| MultiPSK mode name | Behavior documented by that version of MultiPSK | Cross-program bit identity without tests |
| IZ8BLY mode name | Provenance for several amateur inventions where backed by developer records | Current support or adoption |
| ADIF `FMHELL`, `FSKHELL`, `HELL80`, `HELLX5`, `HELLX9`, `PSKHELL`, `SLOWHELL` | A logging vocabulary | A protocol specification or proof a logged contact used the label correctly |
| ADIF `HFSK` | A legacy/import vocabulary lead | A recoverable Hell waveform; the ADIF proposal itself records uncertainty |

Current software breadth is uneven: fldigi offers seven documented profiles; MultiPSK documents historical and amateur profiles including Feld and Hell-80; RadioLib provides a direct Feld implementation; older IZ8BLY programs are important provenance for FM, PSK, Duplo, and multi-tone work. This is support evidence only. Step 4 must measure actual use with denominators. [HELL-EVID-0008] [HELL-EVID-0009] [HELL-EVID-0011] [HELL-EVID-0014] [HELL-EVID-0017] [HELL-EVID-0020]

## Excluded and unresolved candidates

| Candidate | Disposition | Evidence needed to promote it |
| --- | --- | --- |
| PC-Hell / Hell-45 | Unresolved experimental family; reported as asynchronous UART-like 7x5 transmission, but even the developer survey says the Hell-45 relationship is uncertain | Original program/source, author specification, or captures with framing/rate/font |
| HFSK | Excluded as a canonical mode; retained as a logging ambiguity | A normative waveform specification and identified implementation |
| Generic MFSK Hell | Ambiguous family shorthand | Exact distinction from S/MT, C/MT, or Chirp Hell plus tone/raster contract |
| Manufacturer-only labels | Apparatus profile unless a waveform difference is shown | Manual, schematic, or compatible captures proving a distinct contract |
| Font-only variants | Implementation/font profiles | Evidence that geometry or timing changes on-air compatibility rather than appearance alone |

## Source disagreements and normalized resolutions

| Disagreement | Resolution for this taxonomy |
| --- | --- |
| `122.5 baud` versus `245 positions/s` for Feld | Step 3 refinement governs: the historical drum advances through 245 physical positions/s; ordinary marks/spaces have a two-position minimum, yielding the conventional 122.5-baud description, but their phase is not restricted to fixed pairs |
| fldigi table's rounded Slow Hell values versus source constants | Source constants control the implementation profile; derived char rate is labeled calculation |
| `FSK Hell-105` versus `FM Hell-105` | Treat as an implementation-context alias for the 105-baud continuous-phase profile; do not merge generic FSK and FM families globally |
| Historical Hell-80 at 315 baud versus software Hell-80 at 245 baud | Preserve separate historical protocol and fldigi implementation profile; compatibility remains untested |
| Presse-Hell at 5 versus 2.5 char/s | Use a family plus qualified profiles, not one unqualified numerical contract |
| C/MT row counts | Preserve one family with named profiles until exact implementations and interoperability are compared |

## Decisions

- Accept the three-branch family tree and the family/protocol/profile/alias/apparatus vocabulary for the remainder of the spike.
- Retain standard Feld-Hell as the comparison baseline, not as an implementation selection.
- Require qualifiers for Slow Hell, FSK-Hell, Presse-Hell, and Hell-80 wherever ambiguity could affect timing or interoperability.
- Treat historical Hell-80 and fldigi Hell-80 as separate profiles until offline evidence proves a stronger compatibility class.
- Exclude PC-Hell/Hell-45, HFSK, and generic MFSK Hell from candidate ranking unless better specifications are recovered.
- Defer fonts, adoption, interoperability, Wsprry Pi feasibility, and the final recommendation to Steps 3–7.

## Confidence assessment

Confidence is **high** for standard Feld-Hell, the current fldigi profiles, the existence and core parameters of Siemens Hell-80, and the distinction among sequential, two-frequency/phase, and multi-tone mechanisms. It is **moderate** for the full Presse, GL, Duplo, PSK/FM, S/MT, C/MT, and Chirp family details because primary manuals or immutable source are incomplete for some profiles. It is **low** for PC-Hell/Hell-45, HFSK as a waveform, exact Thomson variants, and claims of bit-level interoperability not yet tested.

## Remaining questions

- Step 3 froze the actual-drum Feld and GL references plus immutable modern revisions, while leaving the authoritative 105-baud and historical Hell-80 bitmaps unresolved.
- Which profiles are actually used on amateur bands, and in what proportions? Step 4 needs time-bounded observations and denominators.
- Can current decoders cross-read historical Hell-80, FSK/FM labels, and multi-tone profiles? Step 5 must use offline captures and declared versions.
- Which candidate can fit Wsprry Pi's scheduler and output backends safely and maintainably? Step 6 remains a read-only feasibility analysis.

## Recommended next step

Proceed to **Step 3 — Freeze the font evidence**. Build an immutable, licensed glyph corpus for the historical Feld drum and relevant modern implementations; record source, repertoire, stored/logical/transmitted geometry, traversal, spacing, substitutions, and checksums. Do not select a production font or modify a transmitter.
