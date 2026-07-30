# Step 6 — Evaluate Wsprry Pi feasibility

## Status and bounded conclusion

**Complete as a source-level feasibility study.** Standard Feld-Hell is the only candidate ready to enter Step 7, and it advances **with conditions** rather than as a selected production mode. Wsprry Pi already has the right high-level shape for it: a committed message becomes an absolute-offset sequence of RF-on/RF-off or frequency events, repeat policy uses the compiled duration, and both current backends have interruptible stop and safe-idle paths. A Hell-specific payload, raster compiler, mode wiring, and backend qualification would still be new work. [HELL-EVID-0044] [HELL-EVID-0045] [HELL-EVID-0046]

The three 105-labelled candidates do not advance as equivalent alternatives. fldigi 4.2.12 `FSKH105` and xfhell 3.5.2 `FMHell105` are **deferred** because exact emission requires continuous-phase two-frequency behavior that neither current backend declares, their contracts differ, and each still has a font/licensing or interoperability condition. The documentary six-row/105-baud/55-Hz profile remains **insufficient evidence** because no authoritative licensed bitmap or verified current implementation was recovered. [HELL-EVID-0027] [HELL-EVID-0035] through [HELL-EVID-0037]

This is feasibility, not implementation or hardware qualification. No code, configuration, UI, test, dependency, service, GPIO, audio-device, hardware, or RF operation was performed.

## Decision use and scope

Step 6 answers whether each Step 5 candidate fits Wsprry Pi's source architecture, what class of change it would require, and which gates Step 7 may safely consume. It evaluates:

1. Standard Feld-Hell with an explicit future font and spacing policy.
2. fldigi 4.2.12 `FSKH105`.
3. xfhell 3.5.2 `FMHell105`.
4. The documentary six-row, nominal 105-baud/55-Hz profile.

The study does not choose a production font, design a UI, implement a mode, measure Raspberry Pi timing, measure occupied bandwidth, operate a transmitter, or claim regulatory or deployment readiness.

## Method and evidence states

The Wsprry Pi parent and every submodule were inspected at parent commit `7514ac95b01fbff17065781f69c3c04028bed66f`. Step 1.1 supplies the historical timing baseline; Step 1.2 supplies the hard-gate framework; Steps 2–4 supply taxonomy, font, licensing, and adoption evidence; Step 5 supplies raster and application-interoperability results.

Findings use:

- `OBS`: directly observed in identified source or retained evidence;
- `CALC`: arithmetic reproduced in the [Step 6 artifacts](../artifacts/step-6/README.md);
- `INFER`: an architectural inference that still requires implementation or measurement;
- `HYP`: a testable proposal, not established behavior;
- `UNKNOWN`: insufficient evidence.

No composite score is used. A safety, licensing, timing, or interoperability gate cannot be compensated for by popularity or implementation convenience. [HELL-DEC-0011]

## Wsprry Pi already has a message-to-RF event seam

### Mode and configuration ownership

`ModeType` currently admits WSPR, transient tone, QRSS, FSKCW, and DFCW. Persistent state has distinct mode records for WSPR and the three CW-family modes, while shared CW fields cover message, base frequency, shift, dot duration, spacing, envelope, scheduled start minute/second, and repeat minutes. There is no Hell mode, raster profile, font identity, or audio-output backend. [HELL-EVID-0044]

The complete lifecycle is coupled:

- INI defaults and comments live in `config/wsprrypi.ini`;
- parsing, public JSON, validation, web patching, persistence, and candidate application live in `config_handler.*`;
- direct CLI parsing and validation live in `arg_parser.*`;
- scheduled and immediate execution live in `scheduling.*`;
- the separate UI submodule presents only WSPR, QRSS, FSKCW, and DFCW and mirrors CW duration semantics;
- source regression tests protect mode changes, repeat-duration policy, event compilation, backend dry-run behavior, and selector cleanup.

`OBS`: Hell cannot be added safely as a hidden reuse of `CW.Dot Seconds` or a renamed FSKCW mode. A future mode must have an explicit profile contract and traverse every lifecycle layer.

### Request, compiler, and execution-plan boundary

The transmitter submodule separates:

1. `TransmissionRequest`, which freezes mode, payload, output backend, calibration, execution policy, and metadata;
2. `ExecutionPlanCompiler`, which expands the payload into ordered `RfEvent` records;
3. `ExecutionPlan`, whose events use nanosecond offsets and durations with frequency, RF state, envelope, and source-character index;
4. `TransmissionController`, which prepares a complete plan before executing it through one backend.

Current RF event types are `SET_FREQUENCY`, `RF_ON`, `RF_OFF`, and `HOLD`. QRSS compiles Morse marks to keyed RF and gaps to RF-off events. FSKCW compiles both marks and gaps as RF-on events at one of two frequencies. DFCW combines two keyed frequencies with RF-off gaps. [HELL-EVID-0044]

`INFER`: this plan representation can express a sequential raster as one event per physical position or as run-length-compressed equivalent events. It can also express binary tone selection. That representability does not prove backend timing or continuous phase.

### Scheduler, duration, and cancellation

Non-WSPR schedules resolve the next wall-clock launch using configured minute and second values. A committed transmission then uses monotonic-clock offsets inside its backend. The repeat validator compiles the complete message and rejects a duration longer than `repeat_every`; equality is valid. Configuration reload and explicit stop paths invalidate scheduled work or stop active execution. [HELL-EVID-0046]

Both backends wait interruptibly. The Raspberry Pi backend wraps execution in an RF-off guard; the Si5351 backend idles and disables its output after completion, interruption, or fault. Existing selector-shutdown tests cover cleanup of separate band GPIO state. [HELL-EVID-0045]

`INFER`: a Hell mode can reuse the scheduling policy only after its exact font, spacing, leader/trailer, and event compiler produce the authoritative total duration. A character-count approximation would be unsafe for variable-width fonts.

## Backend capabilities impose the main feasibility boundary

### Raspberry Pi clock/GPIO RF backend

The current Raspberry Pi backend advertises frequency switching, RF gating, fade shape, and precomputed execution. It accepts only WSPR, QRSS, FSKCW, and DFCW plans. QRSS gates the clock without tearing down the committed stream; FSKCW reconstructs one of two compatibility-table symbols while keeping RF enabled; DFCW uses two tones plus RF-off gaps. Event launch is referenced to `CLOCK_MONOTONIC`. [HELL-EVID-0045]

The generic capability structure defaults `supports_continuous_phase` to false, and the backend does not override that field. `OBS`: continuous phase is therefore not a declared contract even though frequency switching exists.

### Si5351 backend

The Si5351 backend advertises the same four positive capabilities and also leaves continuous phase false. It whitelists existing modes and requires exactly one tone for tone/QRSS, two for FSKCW/DFCW, and four for WSPR. Tone changes apply precomputed register sets over I2C; output gating is also performed through the device interface. [HELL-EVID-0045]

`INFER`: one- or two-state Hell plans are structurally representable, but 210–245 register or output transitions per second are not qualified. I2C programming latency, phase behavior, and short-cell output gating require hardware measurement.

### No general audio backend

Only Raspberry Pi clock/GPIO and Si5351 appear in the runtime backend enumeration. Installer sound-card management is not a general audio waveform-output contract. `OBS`: an audio Hell transmitter would be a new backend, not a configuration option on an existing backend.

## Candidate timing and resource calculations

The fixed comparison corpus is `HELL TEST 0123456789 DE WSPRY WSPRY 73`, 38 characters including spaces. For a seven-column cell at 17.5 columns/s:

```text
38 characters × 7 columns / 17.5 columns/s = 15.2 s
```

| Candidate | Physical decision contract | Decision duration | Physical positions for corpus | 128-byte/event upper bound |
| --- | ---: | ---: | ---: | ---: |
| Standard Feld | 14/column at 245/s | 4.081633 ms | 3,724 | 465.5 KiB |
| fldigi `FSKH105` | 14/column at 245/s | 4.081633 ms | 3,724 | 465.5 KiB |
| xfhell `FMHell105` | 12/column at 210/s | 4.761905 ms | 3,192 | 399.0 KiB |
| Documentary six-row/105 | 6 logical rows at 105/s | 9.523810 ms | 1,596 logical cells | 199.5 KiB |

These are `CALC` planning bounds, not measurements. Run-length compression can reduce event count, while variable-width fonts, explicit spacing, and leaders/trailers can change duration. The full arithmetic is retained in [`candidate-calculations.csv`](../artifacts/step-6/candidate-calculations.csv). [HELL-EVID-0047]

Static font storage is small: even 95 characters × 7 columns × 14 one-byte positions is under 10 KiB without bit packing. Planning is linear in transmitted positions. The material resource risk is not capacity but sustaining accurate 4.08–9.52 ms transitions, stop responsiveness, and any required device programming across the oldest claimed Raspberry Pi targets. Build-time low-memory handling does not qualify runtime jitter. [HELL-EVID-0047]

## Candidate 1 — Standard Feld-Hell

### Contract carried from Step 5

- Sequential OOK/ASK raster.
- 17.5 columns/s and 14 physical positions/column, or an explicitly compatible representation.
- Bottom-to-top positions, left-to-right columns.
- Continuous asynchronous stream without character framing.
- Explicit font, leading/trailing spacing, unsupported-character, and blank-cell policy.
- Clean contained F3 in both directions between fldigi 4.2.12 and xfhell 3.5.2, limited to one manual non-blind run. [HELL-EVID-0042]

### Architecture fit

| Area | Classification | Basis and condition |
| --- | --- | --- |
| Plan representation | `DIRECT FIT` conceptually | RF-on/RF-off events with absolute offsets express the raster; a Hell payload/compiler is still absent. |
| Raspberry Pi backend | `BOUNDED EXTENSION` + `HARDWARE-DEPENDENT` | Existing QRSS gating semantics are close, but 4.0816 ms cells, jitter, edges, and safe continuous-stream behavior are unmeasured. |
| Si5351 backend | `BOUNDED EXTENSION` + `HARDWARE-DEPENDENT` | One tone plus output gating fits the model; 245 gates/s and I2C behavior are unqualified. |
| Audio | `NEW BACKEND` | No application audio path exists. |
| Scheduler | `BOUNDED EXTENSION` | Compiled-duration/repeat policy is reusable after exact raster spacing is defined. |
| Font | `UNKNOWN` production asset | Historical transcription lacks redistribution permission; fldigi tables are GPL; RadioLib's MIT table is reproducible but was not the Step 5 F3-qualified font. |

### Disposition

**`ADVANCE WITH CONDITIONS`.** Standard Feld is the only application-qualified interoperability baseline and the simplest waveform family. It may enter Step 7 only with these gates:

1. select an immutable, redistributable font and spacing policy;
2. rerun application interoperability using that exact candidate asset;
3. implement and test raster-to-event compilation offline before enabling any backend;
4. measure timing and cancellation first without RF, then on each claimed backend and supported platform class;
5. define and pass a named spectral criterion before RF qualification.

RadioLib's MIT font can support a licensing-safe prototype, but its different glyphs must not inherit the fldigi↔xfhell F3 result. [HELL-EVID-0026] [HELL-EVID-0038]

## Candidate 2 — fldigi 4.2.12 `FSKH105`

### Contract carried from Step 5

- fldigi common 14-position font path.
- 17.5 columns/s and 245 physical decisions/s.
- Continuous-phase binary tones separated by 55 Hz.
- Selectable fldigi font, with exact interoperability dependent on the chosen table.
- Not F1/F2 equivalent to xfhell `FMHell105`; no reciprocal application test was configurable. [HELL-EVID-0035] [HELL-EVID-0037] [HELL-EVID-0043]

### Architecture fit

The execution plan can describe two RF-on frequencies at 4.0816 ms intervals, but neither backend declares continuous phase. Reusing FSKCW would also be semantically unsafe because its payload is Morse, its validation assumes mark above space, and its compiler emits Morse-duration events rather than raster decisions.

| Area | Classification | Basis and condition |
| --- | --- | --- |
| Plan representation | `DIRECT FIT` conceptually | Binary tone events are representable. |
| Current RF backends | `BOUNDED EXTENSION OR NEW BACKEND` | Source does not establish whether either backend can be extended to preserve phase at 245 switches/s and 55 Hz separation; qualification must determine the implementation boundary. |
| Audio | `NEW BACKEND` | A CPFSK audio path does not exist. |
| Scheduler | `BOUNDED EXTENSION` | Exact compiled duration could reuse repeat policy. |
| Font/license | `CONDITIONAL` | fldigi source tables are GPL; exact asset choice and distribution implications must be resolved. |
| Receiver evidence | `CONDITIONAL` | Native fldigi support exists, but no independent exact-profile F3 endpoint was qualified. |

### Disposition

**`DEFER`.** The candidate has meaningful scheduled-amateur-use evidence and a fully inspectable implementation, but Step 7 must not recommend it until a phase-preserving output path, exact font/license policy, independent receiver target, and offline/hardware qualification plan are established. Popularity evidence cannot waive those gates. [HELL-EVID-0031] [HELL-EVID-0032]

## Candidate 3 — xfhell 3.5.2 `FMHell105`

### Contract carried from Step 5

- Six logical rows represented as 12 physical positions/column.
- 17.5 columns/s and 210 physical decisions/s.
- Continuous-phase tones separated by 210 Hz.
- Paired `FMFatLoEn.bdf` font.
- Not F1/F2 equivalent to fldigi `FSKH105`; no reciprocal exact application test was configurable. [HELL-EVID-0036] [HELL-EVID-0037] [HELL-EVID-0043]

### Architecture fit and disposition

The event abstraction can represent the two tones, and the 4.7619 ms decision interval is slightly longer than fldigi's. The same decisive barriers remain: no backend claims continuous phase, Si5351 transition timing is unmeasured, there is no audio backend, and the specific BDF asset is GPL-derived rather than an approved Wsprry Pi font choice.

**`DEFER`.** xfhell provides a concrete implementation target and font, but not a current cross-application F3 baseline or a phase-qualified Wsprry Pi backend. It must remain separate from fldigi `FSKH105` in configuration, tests, logs, and operator naming.

## Candidate 4 — documentary six-row/105-baud/55-Hz profile

The developer documentation establishes seven columns, six logical rows, 105 baud, 17.5 columns/s, and a special font; Step 5's unresolved question further qualifies the target as the original approximately 55-Hz profile. An authoritative complete font with clear redistribution rights and a verified current implementation were not recovered. [HELL-EVID-0027]

At the logical event layer, 9.5238 ms cells are representable. A continuous-phase 55-Hz waveform would still require the same backend work as the implementation-qualified profiles. Without the actual raster table, spacing, idle behavior, and a receiver target, that architectural observation cannot become an implementation contract.

**`INSUFFICIENT EVIDENCE`.** Do not create a Wsprry Pi mode from documentary dimensions alone. Recovery of a licensed immutable font and a versioned receiver/transmitter implementation is the promotion gate.

## Configuration, CLI, UI, and persistence implications

A future implementation should add a first-class Hell configuration rather than overload CW fields. The minimum durable contract is:

- canonical mode/profile identifier, such as `FELDHELL`, `FLDIGI_FSKH105`, or another implementation-qualified name;
- immutable font/profile identifier and version;
- message and explicit unsupported-character policy;
- RF base or center frequency;
- fixed profile modulation parameters, including shift and phase requirement where applicable;
- inversion only where the receiver contract supports it;
- schedule start minute/second and repeat interval;
- selected output backend;
- transmit-enable gate.

Timing, rows, columns, scan order, spacing, and shift should remain fixed attributes of a named interoperable profile unless research establishes a user-facing reason to expose them. Allowing arbitrary combinations would create private variants and undermine receiver expectations.

Future work would have to update defaults, INI/JSON parsing, validation, internal structs, public configuration JSON, patch/persistence behavior, CLI, UI submodule, scheduler routing, status JSON, logs, source tests, and the independent operator-documentation repository. This report authorizes none of those changes.

## Safety and failure behavior

Any future Hell implementation must preserve these hard gates:

1. **Validate before output:** reject unknown profile/font, unsupported characters, invalid frequency, invalid backend, and overlong repeat policy before committing a request.
2. **Offline separation:** raster and event-plan tests must not instantiate a hardware backend; scheduler suppression and Si5351 dry-run are supporting seams, not substitutes for a dedicated no-output compiler test.
3. **Safe idle:** RF output, amplifier control, TX indication, and band-selector state must begin and end inactive.
4. **Interruptible execution:** stop, reload, shutdown, and mode change must interrupt waits and force backend idle without waiting for a whole character.
5. **No stuck state:** faults during gating or frequency changes must invoke the same RF-off/idle guards as existing modes.
6. **Bounded scheduling:** exact compiled duration must be no longer than the repeat interval; equality may remain valid.
7. **Backend qualification:** a backend may accept a Hell plan only after demonstrating its minimum event duration, frequency accuracy, phase behavior where required, and cancellation latency.

Source inspection supports the existence of these seams, not their Hell-specific behavior. [HELL-EVID-0045] [HELL-EVID-0046]

## Qualification ladder

| Level | Required evidence | What it proves | What it does not prove |
| --- | --- | --- | --- |
| 1. Raster | Exact glyph, traversal, spacing, blank, and unsupported-character fixtures | Font/profile contract | Timing, waveform, receiver behavior |
| 2. Event plan | Deterministic expected events, offsets, frequencies, RF state, duration, and cancellation boundaries | Compiler semantics and repeat calculation | Backend timing |
| 3. Offline waveform/event trace | File-only envelope or backend trace with no device access | Modulation approximation and spectral inputs | Hardware output |
| 4. Application interoperability | Exact candidate asset through versioned receivers in both directions where possible | F3/F4 under declared conditions | Robustness or RF safety |
| 5. Impairments | Frequency/clock offset, inversion, noise, truncation, and dropout matrix | Bounded receiver robustness | Raspberry Pi timing |
| 6. Pi no-transmit timing | Instrumented event timestamps with RF path physically and logically disabled | Platform jitter, load, stop latency | Hardware waveform |
| 7. Attached hardware | Explicitly authorized dummy-load/instrument test | Backend gating, phase, frequency, and fail-safe behavior | On-air suitability |
| 8. RF/on-air | Separately authorized spectral and interoperability protocol | Defined RF criterion and operational behavior | General deployment outside tested conditions |

The private Step 5 rig can be reused at Levels 4–5 after adding the exact proposed Wsprry Pi font/profile fixture. It does not replace Levels 6–8.

## Maintenance and licensing risks

| Candidate | Specification stability | Decoder target | Asset status | Maintenance risk |
| --- | --- | --- | --- | --- |
| Standard Feld | High timing stability; font variants remain | Two application endpoints passed F3 | Historical asset not redistributable; MIT RadioLib alternative not F3-qualified | Moderate |
| fldigi `FSKH105` | Stable only as a version-qualified implementation | fldigi native; no independent exact F3 endpoint | GPL tables require an explicit distribution decision | High |
| xfhell `FMHell105` | Stable only as xfhell 3.5.2 contract | xfhell native; no independent exact F3 endpoint | GPL BDF; original lineage/license separate | High |
| Documentary six-row/105 | Incomplete implementation contract | No verified current exact implementation | Authoritative bitmap/license unknown | Very high |

Wsprry Pi itself and its transmitter submodule are MIT-licensed. This report does not decide whether copying GPL font data into the product is acceptable; that requires an explicit licensing decision. Independently authored compatible assets must be proven independent and retested rather than assumed equivalent.

## Candidate evaluation matrix

| Candidate | Raster/font | Modulation | Output-path fit | Timing/scheduler fit | Receiver evidence | Safety/maintenance | Step 6 disposition |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Standard Feld | 14 positions/column; production font unresolved | Sequential OOK | Plan fit; bounded backend extension; hardware-dependent | 4.0816 ms; duration policy reusable; jitter unmeasured | fldigi↔xfhell clean F3 | Simplest waveform; asset and qualification gates remain | **`ADVANCE WITH CONDITIONS`** |
| fldigi 4.2.12 `FSKH105` | fldigi selectable 14-row GPL table | CPFSK, 55 Hz separation | Bounded extension or new backend, pending phase qualification | 4.0816 ms; plan representable; switching unmeasured | Native fldigi only for exact target | Ambiguous label, GPL asset, no independent exact F3 | **`DEFER`** |
| xfhell 3.5.2 `FMHell105` | paired 12-position GPL BDF | CPFSK, 210 Hz separation | Bounded extension or new backend, pending phase qualification | 4.7619 ms; plan representable; switching unmeasured | Native xfhell only for exact target | Version-specific contract, no independent exact F3 | **`DEFER`** |
| Documentary six-row/105/55 | authoritative table/license unknown | Documentary continuous two-frequency profile | Logical plan representable; exact backend contract incomplete | 9.5238 ms logical; spacing/idle incomplete | No verified current exact endpoint | Missing core asset and implementation target | **`INSUFFICIENT EVIDENCE`** |

## Criteria and hard-gate result

The comparison applies the Step 1.2 hard gates without subjective weighting:

| Criterion | Role | Standard Feld | fldigi `FSKH105` | xfhell `FMHell105` | Documentary six-row |
| --- | --- | --- | --- | --- | --- |
| Versioned software compatibility | Mandatory | Conditional pass: clean F3, limited scope | Blocked: no independent exact F3 | Blocked: no independent exact F3 | Unknown |
| Stable exact contract | Mandatory | Conditional: timing stable, asset open | Version-qualified | Version-qualified | Blocked |
| Current architecture fit | Mandatory | Conditional bounded extension | Extension or new backend pending phase proof | Extension or new backend pending phase proof | Incomplete |
| Safe stop/idle path | Mandatory | Existing seam; Hell test required | Existing seam plus phase-capability backend tests | Existing seam plus phase-capability backend tests | Unknown |
| Licensed reproducible asset | Mandatory | Blocked pending selection | Conditional GPL decision | Conditional GPL decision | Blocked |
| Deterministic offline testing | Mandatory | Feasible | Feasible at source-contract layer | Feasible at source-contract layer | Blocked by asset |
| Clear operator naming | Mandatory | Feasible | Must be version-qualified | Must be version-qualified | Not yet feasible |
| Adoption evidence | Informative | Qualitative lead | Strongest exact-profile schedule lead | Label lineage only | None established |

Standard Feld advances because its remaining blockers are explicit, bounded gates around an already demonstrated application-readable baseline. The 105 profiles remain deferred because they combine backend, asset, and independent-interoperability gaps.

## Decisions affected

- HELL-DEC-0030 accepts the source architecture boundary: Hell requires a first-class raster payload/compiler and explicit backend qualification.
- HELL-DEC-0031 advances Standard Feld with conditions.
- HELL-DEC-0032 defers fldigi `FSKH105`.
- HELL-DEC-0033 defers xfhell `FMHell105`.
- HELL-DEC-0034 retains the documentary six-row profile as insufficient evidence.
- HELL-DEC-0035 limits Step 7 to a gated recommendation, not implementation authorization.

## Limitations, uncertainty, and robustness gaps

- No Wsprry Pi executable or backend ran during Step 6.
- No event-jitter, frequency-switch, I2C-latency, phase, cancellation-latency, CPU, memory, or spectrum measurement was made.
- The 128-byte/event resource figure is a conservative arithmetic bound, not a measured structure size.
- The fixed 15.2-second corpus comparison assumes seven columns per character and excludes profile-specific leaders/trailers.
- Current backend capability flags and source behavior establish contracts, not hardware performance.
- The Standard Feld F3 result is one clean, manual, non-blind application run and does not transfer to an untested production font.
- No legal conclusion is made about incorporating GPL font data into an MIT product.
- Supported Raspberry Pi model breadth is installer policy; no candidate was timed on any model.

## Unresolved questions

- Which immutable, redistributable Standard Feld font and spacing policy should Step 7 gate on?
- Does that exact font remain F3 with fldigi and xfhell?
- Can either current backend hold 4.0816 ms raster timing with acceptable jitter and stop latency on the oldest supported target?
- Can the Raspberry Pi clock or Si5351 preserve continuous phase across 210–245 binary tone transitions per second?
- What named occupied-bandwidth criterion and worst-case raster pattern should later qualification use?
- Is a no-RF timing harness sufficient, or is a dedicated trace backend preferable?
- Can an authoritative licensed six-row/105-baud bitmap and exact maintained decoder be recovered?

## Inputs permitted for Step 7

Step 7 may:

1. compare Standard Feld's conditional path against the documented reasons the other candidates are deferred;
2. recommend an implementation-research direction only if font, interoperability, backend timing, safety, and spectral gates remain explicit;
3. prefer the simplest qualified waveform without claiming deployment readiness;
4. define staged implementation and validation gates.

Step 7 may not:

- call any candidate implemented, hardware-qualified, RF-qualified, or deployment-ready;
- transfer the fldigi↔xfhell F3 result to a different font;
- treat the three 105 candidates as aliases;
- infer continuous phase from frequency-switch capability;
- authorize hardware or on-air work.

## Recommended next step

Proceed to **Step 7 — Make a gated recommendation**. Use Standard Feld as the sole advancing candidate and preserve the font-selection/application-retest gate before any implementation proposal. Keep both implementation-qualified 105 profiles deferred and the documentary profile at insufficient evidence unless new evidence closes their respective blockers.

## Explicit non-claims

This report does not establish implementation completeness, production font selection, backend timing, jitter, phase continuity, frequency accuracy, occupied bandwidth, CPU or memory performance, GPIO safety, Si5351 behavior, transmitter linearity, RF spectral compliance, on-air interoperability, regulatory suitability, installation correctness, or deployment readiness.
