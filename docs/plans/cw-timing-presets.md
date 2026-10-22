# CW Timing Presets

Status: Proposed  
Implementation state: Not implemented  
Repositories affected: `WsprryPi`, `WsprryPi-UI`, and `Wsprry_Pi_Docs` (the separate sibling operator-documentation repository)

## Purpose

Add QRSS1, QRSS3, and QRSS6 speed presets to the WsprryPi web interface while preserving custom CW timing and the existing mode-specific behavior of QRSS, FSKCW, and DFCW.

The speed presets define a shared base timing unit, \(T\):

| Speed preset | Base timing unit |
|---|---:|
| QRSS1 | \(T = 1\) second |
| QRSS3 | \(T = 3\) seconds |
| QRSS6 | \(T = 6\) seconds |
| Advanced | Operator-defined positive \(T\) |

The presets apply to QRSS, FSKCW, and DFCW. They do not make those modes instantiate dots, dashes, tones, or gaps identically.

The design must:

- Default new or incomplete CW timing to QRSS3.
- Apply the selected base timing unit consistently across QRSS, FSKCW, and DFCW.
- Preserve each modulation’s existing element-construction rules.
- Preserve the existing QRSS/FSKCW and DFCW spacing contracts.
- Make standard timing easy to select and understand.
- Preserve existing custom timing configurations.
- Keep advanced controls available without presenting every operator with editable low-level timing fields.
- Preserve the existing backend configuration format.
- Preserve all existing timing keys, including inactive mode-specific values.
- Group related timing controls together in the UI.
- Avoid changing RF, scheduling, or transmitter behavior outside the approved timing-preset presentation.

## Governing Timing Model

The implementation must keep two concepts distinct:

1. **Shared speed contract**
   - QRSS1, QRSS3, QRSS6, and Advanced select the shared base duration \(T\).
   - The existing `CW.Dot Seconds` value stores \(T\).
   - QRSS, FSKCW, and DFCW all consume this shared value.

2. **Mode-specific signal construction**
   - Each modulation constructs its transmitted elements according to its existing runtime behavior.
   - Selecting a speed preset must not replace those mode-specific rules with one universal Morse timing model.

### Mode-Specific Construction

| Modulation | Dot construction | Dash construction | Standard spacing |
|---|---|---|---|
| QRSS | Keyed element lasting \(T\) | Keyed element lasting \(3T\) | Conventional `1/3/7` multipliers |
| FSKCW | Mode-specific tone element lasting \(T\) | Mode-specific tone element lasting \(3T\) | Conventional `1/3/7` multipliers |
| DFCW | Tone-distinguished element lasting \(T\) | Different-tone element also lasting \(T\) | DFCW-specific `0.333333/1/3` multipliers |

The table describes the timing contract that the preset UI must preserve. The existing scheduler and modulation implementations remain authoritative for the exact construction and placement of transmitted elements.

The preset feature must not:

- make a DFCW dash last \(3T\);
- apply conventional `1/3/7` spacing to DFCW;
- replace DFCW’s frequency-distinguished elements with ordinary keyed Morse elements;
- create separate dot-duration values for the three modulation modes.

## Current Behavior

The CW Control panel currently presents settings including:

- CW modulation mode:
  - QRSS
  - FSKCW
  - DFCW
- Dot Seconds
- Frequency Offset
- Base Frequency
- Frequency calibration
- Start minute
- Repeat interval
- QRSS/FSKCW gap multipliers
- DFCW-specific gap multipliers

The backend already uses:

- one shared Dot Seconds value for QRSS, FSKCW, and DFCW;
- one spacing triplet shared by QRSS and FSKCW;
- a separate spacing triplet for DFCW;
- separate runtime construction paths for conventional CW timing and DFCW timing.

Actual gap durations are calculated from:

```text
gap duration = shared base duration T × active gap multiplier
```

### Current Shared Default

```text
Dot Seconds: 3
```

This corresponds to QRSS3.

### Current QRSS/FSKCW Standard Spacing

```text
Intra Element Gap:    1
Inter Character Gap:  3
Inter Word Gap:       7
```

### Current DFCW Standard Spacing

```text
DFCW Intra Element Gap:    0.333333
DFCW Inter Character Gap:  1
DFCW Inter Word Gap:       3
```

These triplets are intentionally different and must remain separately persisted.

## Proposed Operator Model

### Modulation Mode

Continue providing the existing modulation choices:

- QRSS
- FSKCW
- DFCW

The modulation selection controls how the signal is generated. It is separate from the Speed preset.

Changing modulation must:

- preserve the shared Dot Seconds value;
- preserve both stored spacing triplets;
- display the spacing triplet applicable to the selected modulation;
- infer Standard or Advanced spacing from the active triplet;
- recalculate the active mode’s displayed durations;
- not normalize or overwrite either triplet merely because modulation changed.

QRSS and FSKCW use the same conventional spacing triplet. DFCW uses its separate DFCW spacing triplet.

### Speed Preset

Add a Speed control with four choices:

- QRSS1
- QRSS3
- QRSS6
- Advanced

The preset names use established QRSS speed nomenclature, but the selected base duration applies to QRSS, FSKCW, and DFCW.

Recommended supporting text:

```text
Select the shared base duration used by QRSS, FSKCW, and DFCW.
Each modulation retains its own dot, dash, tone, and spacing behavior.
```

QRSS3 is the default when no valid existing shared dot duration is available.

### Dot Duration

Keep Dot Duration visible for every Speed selection.

For QRSS1, QRSS3, and QRSS6:

- Populate Dot Duration with `1`, `3`, or `6`.
- Disable manual editing.
- Display the effective value clearly.
- Explain that the selected Speed preset determines the value.

For Advanced:

- Enable manual editing.
- Preserve the currently effective valid Dot Duration when entering Advanced.
- Require a finite value greater than zero.
- Continue using existing backend validation limits and semantics.

Selecting Advanced must not blank or arbitrarily replace the current duration.

The initial implementation does not retain hidden previous advanced drafts. For example:

```text
Advanced 2.5 → QRSS3 → Advanced
```

results in an editable value of `3`, not restoration of `2.5`.

This is intentional. Hidden draft restoration is outside the initial scope.

### Spacing Mode

Add one visible Spacing control with two choices:

- Standard
- Advanced

The visible control operates on the spacing triplet applicable to the selected modulation.

| Selected modulation | Active spacing triplet |
|---|---|
| QRSS | Shared QRSS/FSKCW triplet |
| FSKCW | Shared QRSS/FSKCW triplet |
| DFCW | DFCW-specific triplet |

Spacing is independent of Speed. Examples include:

- QRSS1 with Standard QRSS spacing
- FSKCW3 with Advanced conventional spacing
- DFCW6 with Standard DFCW spacing
- an Advanced base duration with either Standard or Advanced mode-specific spacing

### Standard Spacing

Standard spacing is mode-aware.

#### QRSS and FSKCW

| Gap | Multiplier |
|---|---:|
| Intra-element | `1` |
| Inter-character | `3` |
| Inter-word | `7` |

#### DFCW

| Gap | Multiplier |
|---|---:|
| Intra-element | `0.333333` |
| Inter-character | `1` |
| Inter-word | `3` |

When Standard is selected:

- Populate only the active triplet with its canonical standard values.
- Disable manual editing of the visible active triplet.
- Continue displaying the fields so the operator can see the effective rules.
- Display the calculated duration for each visible gap.
- Preserve the inactive triplet unchanged.
- Validate the resulting active values.
- Schedule one coherent autosave after state is complete.

Selecting Standard is an intentional configuration change. It replaces custom values only in the active triplet.

Examples:

#### QRSS/FSKCW Standard Spacing

| Speed | Intra-element | Inter-character | Inter-word |
|---|---:|---:|---:|
| QRSS1 | 1 second | 3 seconds | 7 seconds |
| QRSS3 | 3 seconds | 9 seconds | 21 seconds |
| QRSS6 | 6 seconds | 18 seconds | 42 seconds |

#### DFCW Standard Spacing

The persisted DFCW intra-element multiplier is the canonical decimal `0.333333`, not an exact mathematical one-third.

| Speed | Intra-element | Inter-character | Inter-word |
|---|---:|---:|---:|
| QRSS1 | 0.333333 second | 1 second | 3 seconds |
| QRSS3 | 0.999999 second | 3 seconds | 9 seconds |
| QRSS6 | 1.999998 seconds | 6 seconds | 18 seconds |

The UI may format calculated durations for readability, but presentation rounding must not change persisted values or preset inference.

If rounded values are shown, supporting text or accessible detail must avoid implying that the canonical persisted multiplier has changed.

### Advanced Spacing

When Advanced spacing is selected:

- Enable the three fields in the active triplet.
- Preserve their current valid values.
- Require every multiplier to be finite and greater than zero.
- Display calculated durations beside or beneath the active fields.
- Recalculate displayed durations whenever Dot Duration or an active multiplier changes.
- Preserve the inactive triplet unchanged.

The persisted values remain multipliers, not seconds.

The initial implementation does not restore hidden previous advanced spacing drafts. For example:

```text
Advanced spacing → Standard → Advanced spacing
```

retains the newly applied standard values and makes them editable. It does not restore the earlier custom triplet.

## Proposed UI Organization

Place the related timing controls together near the top of CW Control.

Recommended organization:

```text
CW Control

  Modulation
  Mode: [ QRSS ] [ FSKCW ] [ DFCW ]

  CW Timing
  Speed: [ QRSS1 ] [ QRSS3 ] [ QRSS6 ] [ Advanced ]

  Dot duration:
  [ 3 seconds — disabled ]
  QRSS3 provides a shared base duration of three seconds.

  Spacing:
  [ Standard ] [ Advanced ]

  Intra-element:
  [ active multiplier — disabled ] × base duration = calculated duration

  Inter-character:
  [ active multiplier — disabled ] × base duration = calculated duration

  Inter-word:
  [ active multiplier — disabled ] × base duration = calculated duration

  Mode-specific explanation:
  QRSS and FSKCW use conventional 1×/3×/7× spacing.
  DFCW uses its existing 0.333333×/1×/3× spacing and
  frequency-distinguished equal-duration elements.

  Frequency and Schedule
  Base frequency | Frequency offset
  Frequency calibration
  Start minute | Repeat interval
```

Only the spacing triplet applicable to the selected modulation should be presented as active.

The UI may:

- reuse the existing separate QRSS/FSKCW and DFCW controls;
- show and hide the applicable triplet;
- or bind one visible presentation to separate underlying values.

Regardless of implementation, both persisted triplets must survive loading, editing, modulation changes, and saving.

### Narrow-Screen Order

On narrow screens, controls should stack in this order:

1. Modulation mode
2. Speed preset
3. Dot Duration
4. Spacing mode
5. Active gap controls
6. Mode-specific timing explanation
7. Frequency controls
8. Scheduling controls

## Configuration Compatibility

### Existing Timing Keys

Continue preserving all existing timing values:

```json
{
  "CW": {
    "Dot Seconds": 3.0,
    "Intra Element Gap": 1.0,
    "Inter Character Gap": 3.0,
    "Inter Word Gap": 7.0,
    "DFCW Intra Element Gap": 0.333333,
    "DFCW Inter Character Gap": 1.0,
    "DFCW Inter Word Gap": 3.0
  }
}
```

Confirm the exact spelling and capitalization against the current configuration contract before implementation.

The timing inventory consists of:

- one shared Dot Seconds value;
- three QRSS/FSKCW spacing values;
- three DFCW spacing values.

Do not require new backend configuration keys for the initial implementation.

In particular, do not initially persist:

- `Speed Preset`
- `QRSS Mode`
- `Advanced Dot Timing`
- `Spacing Mode`
- `Advanced Spacing`

These are UI interpretations of effective persisted values.

### Speed Inference

When loading configuration:

| Persisted Dot Seconds | Selected Speed |
|---:|---|
| Exactly `1` | QRSS1 |
| Exactly `3` | QRSS3 |
| Exactly `6` | QRSS6 |
| Any other valid positive value | Advanced |

Use strict numeric equality after finite-number parsing.

Preset values are exactly representable and are written directly by the UI. Do not use fuzzy comparison that would classify a deliberate custom value such as `3.0000001` as QRSS3.

### Mode-Aware Spacing Inference

Infer Spacing independently from the active triplet.

#### QRSS or FSKCW Selected

| Active persisted multipliers | Selected Spacing |
|---|---|
| Exactly `1`, `3`, and `7` | Standard |
| Any other valid positive combination | Advanced |

#### DFCW Selected

| Active persisted multipliers | Selected Spacing |
|---|---|
| Exactly `0.333333`, `1`, and `3` | Standard |
| Any other valid positive combination | Advanced |

Use the exact canonical persisted DFCW constant `0.333333`. Do not infer Standard by comparing against an independently calculated one-third value.

The inactive triplet does not determine the visible Spacing selection.

Examples:

- QRSS selected, conventional triplet standard, DFCW triplet custom:
  - visible Spacing is Standard;
  - DFCW custom values remain untouched.
- DFCW selected, DFCW triplet custom, conventional triplet standard:
  - visible Spacing is Advanced;
  - conventional standard values remain untouched.
- Switching between those modes changes the visible inference without modifying either triplet.

### Defaults

When timing keys are absent, use:

```text
Speed:                         QRSS3
Dot Seconds:                   3

QRSS/FSKCW spacing:
  Intra Element Gap:           1
  Inter Character Gap:         3
  Inter Word Gap:              7

DFCW spacing:
  DFCW Intra Element Gap:      0.333333
  DFCW Inter Character Gap:    1
  DFCW Inter Word Gap:         3
```

An existing valid configuration takes precedence over these defaults.

### Missing, Invalid, and Malformed Values

Treat these cases separately:

1. **Absent key**
   - Apply the established default for that key.
   - Do not disturb other present valid values.

2. **Present numeric but non-finite or non-positive value**
   - Preserve visible evidence of invalid input when the current data path can represent it.
   - Select Advanced for the affected active timing group.
   - Present validation requiring correction.
   - Do not silently normalize corruption into a valid preset unless existing backend behavior prevents the value from reaching the UI.

3. **Wrong JSON type or structurally malformed configuration**
   - Preserve the backend’s configuration parsing error behavior.
   - Do not conceal a parsing failure by treating it as an absent value.

Implementation must inspect the existing loader and backend parser before finalizing invalid-value behavior. It must not weaken existing configuration validation.

## State-Transition Rules

### Selecting QRSS1, QRSS3, or QRSS6

When an operator selects a preset:

1. Set shared Dot Seconds to `1`, `3`, or `6`.
2. Disable manual editing of Dot Duration.
3. Preserve both spacing triplets.
4. Recalculate displayed durations for the active modulation.
5. Revalidate the effective active timing and message duration.
6. Schedule one autosave after state is coherent.

The selected Speed persists across modulation changes because Dot Seconds is shared.

### Selecting Advanced Speed

When an operator selects Advanced:

1. Retain the currently effective valid Dot Seconds.
2. Enable Dot Duration.
3. Do not erase, replace, or unexpectedly select the value.
4. Preserve both spacing triplets.
5. Revalidate through the existing live-validation workflow.
6. Schedule autosave only when the effective value actually changes.

### Selecting Standard Spacing

When an operator selects Standard:

1. Determine the active triplet from the selected modulation.
2. For QRSS or FSKCW, set only the shared conventional triplet to `1/3/7`.
3. For DFCW, set only the DFCW triplet to `0.333333/1/3`.
4. Preserve the inactive triplet unchanged.
5. Disable manual editing of the active fields.
6. Recalculate displayed active gap durations.
7. Revalidate the effective active timing and message duration.
8. Schedule one autosave after state is coherent.

### Selecting Advanced Spacing

When an operator selects Advanced spacing:

1. Determine the active triplet from the selected modulation.
2. Retain the active triplet’s current values.
3. Enable the active fields.
4. Preserve the inactive triplet unchanged.
5. Continue showing calculated active durations.
6. Revalidate through the existing live-validation workflow.
7. Do not autosave merely because controls became editable if values did not change.

### Changing Modulation

When changing between QRSS, FSKCW, and DFCW:

1. Preserve shared Dot Seconds.
2. Preserve the QRSS/FSKCW spacing triplet.
3. Preserve the DFCW spacing triplet.
4. Select the newly active triplet.
5. Infer Standard or Advanced from that triplet.
6. Update field visibility and enablement.
7. Update mode-specific explanatory text.
8. Recalculate active durations and message duration.
9. Do not normalize the newly active triplet.
10. Do not overwrite the newly inactive triplet.
11. Follow the existing guarded mode-change and autosave policy.

A modulation change must not be treated as a request to apply Standard spacing.

### Loading Existing Configuration

During configuration population:

1. Load shared Dot Seconds.
2. Load the complete QRSS/FSKCW spacing triplet.
3. Load the complete DFCW spacing triplet.
4. Infer Speed from Dot Seconds.
5. Determine the active triplet from the selected modulation.
6. Infer visible Spacing from the active triplet.
7. Set visible values, help text, and enabled states.
8. Calculate active gap durations and message duration.
9. Preserve inactive values in UI state.
10. Avoid triggering autosave or normalization.
11. Synchronize the autosave baseline only after population is complete.

Configuration loading must not overwrite custom timing merely because preset controls now exist.

### Repairing an Invalid Inactive Spacing Triplet

All seven timing values must be valid before autosave. If the inactive spacing triplet is invalid:

1. Block autosave without normalizing the invalid values or changing modulation.
2. Identify the invalid inactive group in the save-status feedback.
3. Provide a `Review QRSS/FSKCW spacing` action when the conventional triplet is invalid, or a `Review DFCW spacing` action when the DFCW triplet is invalid.
4. When invoked, reveal a temporary inactive-triplet repair section without changing `Operation.Mode`.
5. Focus the first invalid field in the revealed group.
6. Keep the unresolved invalid group revealed while saving remains blocked; do not hide it automatically or allow a close action to hide it.
7. Revealing the repair section must not mutate timing values or trigger autosave.
8. If the operator closes the repair section after its fields are valid, closing it must not mutate values, change `Operation.Mode`, or trigger autosave.
9. After all seven timing values become valid, validate once and schedule one coherent autosave.

Do not automatically switch modulation to expose an invalid inactive triplet. The repair section is a temporary editing surface for preserved inactive values, not a modulation change or a second persisted spacing mode.

### Disabled Fields and Serialization

Disabled timing fields still represent active configuration values.

Before collecting the configuration payload:

- Read canonical effective timing state rather than relying on native browser form-submission semantics.
- Include shared Dot Seconds.
- Include all three QRSS/FSKCW gap multipliers.
- Include all three DFCW gap multipliers.
- Preserve inactive custom values.
- Never treat a disabled or hidden timing control as absent.
- Never derive the inactive triplet from the active triplet.
- Serialize all existing timing keys with their existing numeric semantics.

## Central Timing-State Requirement

Do not scatter preset, enablement, validation, calculation, and autosave behavior among unrelated event handlers.

Implement testable timing-state functions that can:

- parse Dot Seconds;
- infer Speed;
- identify the active spacing triplet;
- infer mode-aware Spacing;
- apply a Speed transition;
- apply a Standard or Advanced spacing transition;
- preserve the inactive triplet;
- compute calculated gap durations;
- expose canonical effective values for serialization;
- synchronize controls without autosaving during population.

Keep pure timing decisions independent of DOM mutation and autosave where practical.

Each intentional selector transition should follow one coherent path:

1. update canonical values;
2. update visible controls;
3. update enabled states and help text;
4. recalculate durations;
5. validate once;
6. schedule no more than one autosave.

Configuration population must use a non-saving synchronization path.

### Autosave Feedback for Inactive Timing Errors

When autosave is blocked by an invalid inactive triplet, save-status feedback must:

- state that settings were not saved;
- name either QRSS/FSKCW spacing or DFCW spacing as the group requiring correction;
- expose the applicable review action;
- keep the action available until the group is valid;
- avoid implying that the active modulation or its active spacing is invalid when it is not; and
- transition through one validation and one coherent autosave after all seven timing values become valid.

Opening or closing the repair section is navigation and disclosure only. Neither action may mark the configuration dirty, issue a PATCH, schedule autosave, change `Operation.Mode`, or alter the autosave baseline.

## Validation

### Shared Dot Duration

Dot Duration must be:

- numeric;
- finite;
- greater than zero.

When a preset is selected, validate the canonical preset value even if the visible input is disabled.

Do not rely solely on existing validation behavior that treats a disabled input as valid.

Finite-number validation must be enforced symmetrically for Dot Seconds and all six gap values across every supported input path. A small parent-backend change is permitted when needed to make candidate validation, configuration parsing, command-line parsing, and web PATCH validation enforce the same finite-and-positive contract. This does not authorize a schema change or alteration of valid timing semantics.

### Gap Multipliers

All six persisted gap multipliers must be:

- numeric;
- finite;
- greater than zero.

The visible validation state should emphasize the active triplet, but serialization must never replace an invalid inactive value silently. An invalid inactive triplet blocks autosave and uses the temporary repair workflow defined above.

When Standard is selected:

- QRSS/FSKCW canonical values are `1/3/7`;
- DFCW canonical values are `0.333333/1/3`.

### Message Duration and Repeat Interval

Existing mode-specific message-duration and repeat-interval validation remains applicable.

The implementation must continue detecting configurations in which the calculated message duration exceeds the configured repeat interval.

Duration calculation must use:

- shared Dot Seconds;
- the selected modulation’s element-construction rules;
- the selected modulation’s active spacing triplet;
- the actual message.

This is especially important for:

- QRSS6;
- FSKCW6;
- DFCW6;
- long messages;
- large advanced gap multipliers;
- custom Dot Duration;
- modulation changes that activate a different spacing triplet.

The UI estimate and backend repeat-policy calculation must agree for equivalent inputs.

## Accessibility and Interaction Requirements

- Use a fieldset and legend or equivalent accessible grouping for Speed.
- Use a separate accessible group for Spacing.
- Associate Spacing with the selected modulation’s timing context.
- Do not communicate disabled, active, standard, or advanced state through color alone.
- Associate help and calculated-duration output with applicable controls.
- Keep keyboard operation complete and predictable.
- Preserve visible focus indicators.
- Ensure disabled values remain legible in light and dark themes.
- Announce meaningful calculated-duration changes without excessive screen-reader chatter.
- Use operator-facing terminology rather than internal variable or JSON names.
- Preserve visible validation feedback for advanced editable fields.
- Identify an invalid inactive spacing group in save-status feedback using its operator-facing name.
- Make each inactive-group review action keyboard operable and expose a clear accessible name.
- When a review action reveals the repair section, move focus to the first invalid field and announce the newly available editing context without excessive chatter.
- Keep an unresolved invalid repair section exposed while autosave remains blocked.
- Return focus predictably when a valid repair section is closed, without changing modulation or timing values.
- Explain how to enable editing when a field is disabled.
- Explain that QRSS speed names select a shared base duration without implying identical modulation construction.

Suggested labels:

```text
Modulation
Speed
Dot duration
Spacing
Intra-element gap
Inter-character gap
Inter-word gap
```

Suggested supporting text:

```text
QRSS3 selects a shared base duration of three seconds.

QRSS and FSKCW use conventional dot, dash, and spacing timing.

DFCW uses equal-duration, frequency-distinguished elements and
DFCW-specific spacing.

Select Advanced to enter a custom base duration.

Select Advanced spacing to edit the active mode’s gap multipliers.
```

## Impeccable Requirement

This is UI work and must use the Impeccable skill as required by `AGENTS.md`.

Before implementation:

1. Confirm that Impeccable is installed and usable.
2. Read its instructions completely.
3. Load the required `WsprryPi-UI` product and design context.
4. Use the product register appropriate to the technical appliance interface.
5. Inspect existing desktop, mobile, light-theme, and dark-theme behavior.

During and after implementation:

1. Use Impeccable to review hierarchy and progressive disclosure.
2. Preserve the restrained bench-instrument design language.
3. Render the actual Setup page.
4. Exercise every modulation and timing combination.
5. Inspect desktop and mobile layouts.
6. Inspect both supported themes.
7. Review disabled, enabled, invalid, saving, saved, and failed-save states.
8. Address applicable findings.
9. Report findings intentionally not adopted and why.
10. Finish with an Impeccable polish pass.

If Impeccable is unavailable, stop before changing UI files.

Do not commit `.agents/`, `.impeccable/`, `.codex/`, or other local runtime state unless explicitly requested.

## Repository Boundaries

### `WsprryPi-UI`

UI work belongs in the root `WsprryPi-UI` submodule, including:

- timing-control markup;
- labels and help text;
- responsive layout;
- Speed and Spacing selection;
- mode-aware enablement;
- calculated-duration presentation;
- configuration population;
- canonical timing-state logic;
- autosave interaction;
- UI validation;
- behavioral UI tests.

Inspect the submodule at the exact commit recorded by the parent repository before making changes.

### `WsprryPi`

Parent-repository work may include:

- UI/source regression tests;
- integration-contract tests;
- configuration compatibility tests;
- mode-specific message-duration coverage;
- repeat-policy regression coverage;
- documentation or planning references;
- a reviewed `WsprryPi-UI` submodule-pointer update.

The backend configuration schema should not require modification for the initial implementation.

Existing backend source may require no functional change if the UI can preserve its current seven-value timing contract. Do not modify backend code merely to make the change appear symmetrical.

The engineering implementation contract remains at `WsprryPi/docs/plans/cw-timing-presets.md`. Operator-facing documentation does not belong in this repository; it belongs in the separate sibling `Wsprry_Pi_Docs` repository.

### `Wsprry_Pi_Docs`

Operator documentation lives in the separate sibling Git repository `../Wsprry_Pi_Docs`. It is not a `WsprryPi` submodule and must be treated as an independent repository boundary.

Before working there:

- inspect and follow that repository's own `AGENTS.md`;
- inspect and preserve its current branch and working tree, including all existing user changes;
- obtain explicit authorization for cross-repository writes;
- build and render the documentation using that repository's documented workflow;
- use Impeccable to review affected rendered HTML;
- replace screenshots only when the implemented interface makes an existing image materially inaccurate; and
- keep its diff, review, commit, and push boundary separate from `WsprryPi` and `WsprryPi-UI`.

If cross-repository documentation changes are not authorized, inspect documentation impact and provide a follow-up report without modifying `Wsprry_Pi_Docs`.

### `src/` Dependency Submodules

No changes to dependency submodules under `src/` are expected.

If implementation appears to require such a change, stop and report:

- the affected submodule;
- why parent or UI changes are insufficient;
- the proposed dependency modification;
- required validation.

Do not modify a dependency without explicit approval.

## Expected Implementation Seams

Confirm current paths and contracts before implementation.

Likely UI seams:

```text
WsprryPi-UI/data/views/config.php
WsprryPi-UI/data/index.js
WsprryPi-UI/data/site.js
WsprryPi-UI/data/index.css
WsprryPi-UI/data/site.css
```

Likely parent-repository seams:

```text
src/config_handler.hpp
src/config_handler.cpp
src/arg_parser.cpp
src/scheduling.cpp
src/tests/ui_source_regression_test.cpp
src/tests/dial_frequency_semantics_test.cpp
src/tests/non_wspr_repeat_policy_test.cpp
src/tests/qrss_execution_regression_test.cpp
```

Do not modify every listed file automatically. Inspect current contracts and change only what the approved implementation requires.

## UI Behavioral Test Requirement

The current source-fragment regression test is insufficient to prove state transitions, serialization, accessibility state, or autosave behavior.

Add a small behavioral test harness suitable for the current UI architecture.

Prefer:

- testable pure timing-state functions;
- a lightweight DOM-capable JavaScript test layer where DOM behavior must be exercised;
- deterministic tests that do not require live hardware;
- no unnecessary production dependency expansion.

The exact harness may be selected during implementation, but string-search assertions alone do not satisfy the behavioral acceptance criteria.

## Required Test Coverage

### Speed Tests

Verify:

- absent Dot Seconds defaults to QRSS3;
- QRSS1 sets Dot Seconds to `1`;
- QRSS3 sets Dot Seconds to `3`;
- QRSS6 sets Dot Seconds to `6`;
- preset Dot Duration is disabled;
- Advanced enables Dot Duration;
- entering Advanced retains the current effective value;
- custom positive Dot Duration loads as Advanced;
- `3.0000001` loads as Advanced, not QRSS3;
- speed selection persists across QRSS, FSKCW, and DFCW changes;
- speed changes preserve both spacing triplets.

### Mode-Aware Spacing Tests

Verify:

- QRSS with `1/3/7` infers Standard;
- FSKCW with `1/3/7` infers Standard;
- DFCW with `0.333333/1/3` infers Standard;
- a noncanonical conventional triplet infers Advanced;
- a noncanonical DFCW triplet infers Advanced;
- mathematical one-third does not replace canonical `0.333333`;
- Standard QRSS or FSKCW writes only `1/3/7`;
- Standard DFCW writes only `0.333333/1/3`;
- Standard disables the active fields;
- Advanced enables the active fields;
- selecting Standard preserves the inactive triplet;
- selecting Advanced does not change either triplet;
- switching modulation changes visible inference without normalization;
- QRSS and FSKCW expose the same shared triplet;
- DFCW exposes its separate triplet;
- custom inactive values survive modulation changes and saving.

### Calculated-Duration Tests

#### QRSS/FSKCW Cases

| Dot Duration | Multipliers | Expected gap durations |
|---:|---|---|
| 1 | `1/3/7` | `1/3/7` seconds |
| 3 | `1/3/7` | `3/9/21` seconds |
| 6 | `1/3/7` | `6/18/42` seconds |
| 2.5 | `1/3/7` | `2.5/7.5/17.5` seconds |
| 3 | `1.5/4/8` | `4.5/12/24` seconds |

#### DFCW Cases

| Dot Duration | Multipliers | Exact calculated gap durations |
|---:|---|---|
| 1 | `0.333333/1/3` | `0.333333/1/3` seconds |
| 3 | `0.333333/1/3` | `0.999999/3/9` seconds |
| 6 | `0.333333/1/3` | `1.999998/6/18` seconds |
| 2.5 | `0.333333/1/3` | `0.8333325/2.5/7.5` seconds |
| 3 | `0.5/2/4` | `1.5/6/12` seconds |

Verify calculations before presentation formatting.

Presentation rounding must not:

- change persisted values;
- affect Standard inference;
- change message-duration calculations;
- rewrite canonical `0.333333`.

### Element-Construction Tests

Verify that preset selection changes only shared \(T\), not mode construction:

- QRSS dot remains \(T\);
- QRSS dash remains \(3T\);
- FSKCW dot remains \(T\);
- FSKCW dash remains \(3T\);
- DFCW dot remains a tone-distinguished element of \(T\);
- DFCW dash remains a different-tone element of \(T\);
- DFCW is not converted to a \(T/3T\) keyed-element model;
- existing mode-specific message duration remains consistent with runtime scheduling.

### Persistence Tests

Verify that collection continues emitting all existing timing keys:

```text
CW.Dot Seconds
CW.Intra Element Gap
CW.Inter Character Gap
CW.Inter Word Gap
CW.DFCW Intra Element Gap
CW.DFCW Inter Character Gap
CW.DFCW Inter Word Gap
```

Confirm exact key names against current source.

Verify:

- preset values save correctly;
- disabled fields remain serialized;
- hidden inactive values remain serialized;
- Advanced values survive save and reload;
- both custom triplets survive round-trip;
- saving QRSS or FSKCW does not reset DFCW spacing;
- saving DFCW does not reset QRSS/FSKCW spacing;
- loading configuration does not cause normalization autosave;
- population causes no PATCH or autosave;
- one selector action causes no more than one coherent autosave;
- no new backend keys are required.

### Invalid-Value Tests

Verify:

- absent values use documented defaults;
- zero is rejected;
- negative values are rejected;
- blank advanced inputs are rejected;
- non-finite-equivalent values are rejected;
- wrong JSON types retain existing backend error behavior;
- disabled preset controls serialize canonical valid values;
- invalid custom values are not silently relabeled as presets;
- validation identifies the applicable active controls;
- inactive values are not silently discarded;
- all seven timing values must be valid before autosave;
- an invalid inactive triplet blocks autosave without normalization or a modulation change;
- save-status feedback identifies the invalid inactive group;
- an invalid conventional triplet provides `Review QRSS/FSKCW spacing`;
- an invalid DFCW triplet provides `Review DFCW spacing`;
- the review action reveals the applicable temporary repair section and focuses its first invalid field;
- revealing the repair section does not change `Operation.Mode`, mutate values, mark the configuration dirty, or trigger autosave;
- an unresolved invalid repair section cannot be hidden while autosave remains blocked;
- closing a valid repair section does not change `Operation.Mode`, mutate values, or trigger autosave;
- correcting the last invalid timing value validates once and schedules one coherent autosave; and
- the repair workflow never automatically switches modulation.

### Backend Regression Tests

Verify:

- Dot Seconds remains shared across QRSS, FSKCW, and DFCW;
- QRSS and FSKCW continue sharing conventional gap values;
- DFCW continues using separate gap values;
- all seven timing values load, validate, serialize, and round-trip;
- Dot Seconds and all six gap values receive symmetrical finite-and-positive validation across supported input paths;
- conventional timing produces expected runtime durations;
- DFCW timing produces expected runtime durations;
- message-duration calculations include mode-appropriate spacing;
- repeat-policy rejection works for messages longer than the interval;
- existing configurations without UI metadata remain valid;
- no backend migration is required.

### Visual and Workflow Tests

Using Impeccable and the rendered application, verify:

- desktop layout at approximately `1440 × 900`;
- narrow/mobile layout at approximately `390 × 844`;
- light theme;
- dark theme;
- QRSS, FSKCW, and DFCW;
- QRSS1, QRSS3, and QRSS6;
- Advanced Dot Duration;
- Standard and Advanced mode-aware spacing;
- mode switching with one standard and one custom triplet;
- mode switching with both triplets custom;
- disabled-field legibility;
- calculated-duration updates;
- message-duration and repeat validation;
- autosave feedback;
- blocked autosave with an invalid inactive QRSS/FSKCW triplet and its review action;
- blocked autosave with an invalid inactive DFCW triplet and its review action;
- temporary inactive-triplet repair disclosure, first-invalid-field focus, and predictable close focus;
- unresolved invalid repair content remaining visible while saving is blocked;
- repair disclosure opening and closing without modulation, value, dirty-state, or autosave side effects;
- one coherent autosave after the final invalid timing value becomes valid;
- reload restoration;
- keyboard order and radio-group behavior;
- focus behavior when entering Advanced;
- accessible group names and descriptions;
- live-output announcements;
- saving, saved, invalid, and failed-save states.

Automated tests do not replace rendered-page workflow inspection.

## Automated Validation

After implementation, inspect current Makefile targets before running them.

Expected safe parent-repository validation from `src` includes:

```sh
make semantics-test
make non-wspr-repeat-policy-test
make qrss-execution-regression-test
```

Also run the new UI behavioral test target from the appropriate UI repository location.

Do not run:

```sh
make test
make test-tone
make test-oneshot
```

unless separately authorized after inspecting their operational behavior. These may use elevated privileges or exercise transmitter paths.

Final static checks must include:

```sh
git diff --check
git status --short --branch
git submodule status --recursive
git submodule foreach --recursive 'git status --short --branch'
```

Report parent and submodule state separately.

## Acceptance Criteria

The feature is acceptable when:

- CW Speed defaults to QRSS3 when no valid Dot Seconds value is available.
- Operators can select QRSS1, QRSS3, or QRSS6 directly.
- The presets select shared \(T\) values of one, three, or six seconds.
- Dot Duration is editable only in Advanced Speed.
- Speed selection applies to QRSS, FSKCW, and DFCW.
- QRSS and FSKCW preserve their existing \(T/3T\) element timing.
- DFCW preserves equal-duration, frequency-distinguished elements.
- Standard QRSS/FSKCW spacing produces `1/3/7`.
- Standard DFCW spacing produces `0.333333/1/3`.
- Advanced spacing edits only the active triplet.
- Changing modulation never normalizes or overwrites either triplet.
- Both triplets survive loading, switching, saving, and reloading.
- Calculated durations reflect the active modulation and spacing.
- Timing controls are grouped coherently.
- Existing custom configurations round-trip without loss.
- All seven existing timing values remain preserved.
- All seven timing values must be finite and positive before autosave.
- An invalid inactive triplet blocks autosave without normalization or automatic modulation changes.
- Save-status feedback names the invalid inactive spacing group and provides the applicable review action.
- The review action reveals a temporary repair section and focuses the first invalid field without mutating configuration state or triggering autosave.
- An unresolved invalid repair section remains visible while saving is blocked.
- Correcting the final invalid timing value validates once and schedules one coherent autosave.
- Dot Seconds and all six gap values receive symmetrical finite-number validation across supported input paths, with a small parent-backend consistency change allowed if required.
- No backend configuration migration is required.
- UI behavioral tests cover transitions, persistence, validation, and autosave.
- Backend regression and repeat-policy tests pass.
- Impeccable review and rendered-page inspection are complete.
- Documentation impact has been reviewed.
- When cross-repository documentation changes are authorized, required operator documentation in `Wsprry_Pi_Docs` is updated, rendered using that repository's workflow, and reviewed with Impeccable.
- When cross-repository documentation changes are not authorized, the required `Wsprry_Pi_Docs` follow-up is reported without implying that a documentation write was required or completed.
- No `src/` dependency submodule changes are required.
- UI, parent-repository, and operator-documentation changes remain separately reviewable.
- Hardware or live-transmission validation is not claimed unless separately authorized and performed.

## Non-Goals

This initial feature does not:

- make all modulation modes instantiate identical elements;
- change QRSS or FSKCW dash duration from \(3T\);
- change DFCW to a \(T/3T\) element model;
- change DFCW’s frequency-distinguished dot/dash behavior;
- replace DFCW Standard spacing with `1/3/7`;
- combine the two persisted spacing triplets;
- create different Dot Seconds values for each modulation;
- introduce persisted preset names;
- introduce persisted Spacing-mode names;
- restore hidden previous advanced drafts;
- automatically switch modulation to repair an invalid inactive spacing triplet;
- normalize, discard, or conceal an invalid inactive triplet;
- treat opening or closing the temporary inactive-triplet repair section as a configuration change;
- change the backend configuration schema;
- alter RF frequency behavior;
- alter Frequency Offset semantics;
- alter scheduling semantics outside recalculation from existing timing;
- redesign the entire Setup page;
- modify transmitter hardware behavior;
- modify dependency submodules under `src/`;
- authorize live RF, GPIO, service, installation, or hardware testing;
- commit or push any repository.

## Documentation Impact

Operator documentation lives in the separate sibling `Wsprry_Pi_Docs` repository, not in `WsprryPi`. Before editing, verify the current documentation structure and content rather than assuming prior inspection remains current.

Known affected areas from prior inspection are:

- `docs/User_Interface/Setup/Signal_Setup/index.md`;
- `docs/Advanced_Operations/index.md`; and
- `docs/Command_Line_Operations/index.md`.

Current verification must determine which of these files, and whether any additional files, actually require changes for the implemented behavior.

Documentation impact review must cover:

- CW modulation selection;
- shared QRSS1, QRSS3, and QRSS6 Speed presets;
- custom Dot Duration;
- QRSS element timing;
- FSKCW element timing;
- DFCW equal-duration, frequency-distinguished elements;
- conventional QRSS/FSKCW spacing;
- DFCW-specific spacing;
- calculated durations;
- repeat intervals;
- Signal Setup;
- screenshots depicting CW Control.

Documentation must clearly distinguish:

- modulation from Speed;
- shared \(T\) from mode-specific element construction;
- QRSS/FSKCW spacing from DFCW spacing;
- multiplier values from calculated seconds;
- Standard from Advanced behavior;
- active from preserved inactive values.

Recommended documentation wording:

```text
QRSS1, QRSS3, and QRSS6 select the shared base duration used by
QRSS, FSKCW, and DFCW. Each modulation retains its own signal
construction. QRSS and FSKCW use conventional dot, dash, and
spacing timing. DFCW uses equal-duration elements distinguished
by frequency and retains its DFCW-specific spacing.
```

Replace screenshots only when the changed interface makes an existing image materially inaccurate. Age alone is not a reason to replace a contextually correct screenshot.

Do not include internal test or refactoring details in operator documentation unless they materially help the operator.

## Submodule and Commit Boundaries

The implementation may involve three independent Git repository boundaries:

1. the `WsprryPi-UI` submodule for UI implementation;
2. the parent `WsprryPi` repository for tests, the reviewed UI pointer, and this engineering plan; and
3. the separate sibling `Wsprry_Pi_Docs` repository for operator documentation when cross-repository writes are explicitly authorized.

If commits are later authorized:

1. Review the complete UI submodule diff.
2. Run UI behavioral and Impeccable validation.
3. Commit the UI change inside `WsprryPi-UI`.
4. Ensure the UI commit is pushed to its intended remote before publishing a parent commit that points to it.
5. Review parent integration tests, the exact old and new UI submodule commit IDs, the pointer update, and the engineering-plan change.
6. Commit the parent submodule-pointer update, tests, and plan separately or as an explicitly reviewed parent change.
7. In `Wsprry_Pi_Docs`, independently review the authorized operator-documentation diff, render the documentation using that repository's workflow, and complete Impeccable review of affected rendered HTML.
8. Commit documentation changes independently in `Wsprry_Pi_Docs`.
9. Push only the repositories explicitly authorized, preserving the three separate publication boundaries.

Do not modify or advance unrelated `src/` submodules.

## Implementation Sequence

1. Reconfirm the current parent and recursive submodule state.
2. Confirm the seven-key timing inventory and exact key spelling.
3. Confirm the current QRSS, FSKCW, and DFCW runtime timing constructors.
4. Confirm Impeccable availability and load the UI product/design context.
5. Add testable mode-aware timing-state functions.
6. Add behavioral tests for inference, transitions, preservation, calculations, serialization, validation, and autosave.
7. Add accessible Speed and Spacing controls.
8. Wire one coherent transition path per selector.
9. Preserve non-saving configuration population.
10. Reorganize the panel into timing, frequency, and scheduling groups.
11. Extend parent source-contract and backend regression coverage.
12. Run safe automated tests.
13. Render and inspect all required UI states with Impeccable.
14. Review operator-documentation impact in the separate sibling `Wsprry_Pi_Docs` repository.
15. Only with explicit cross-repository authorization, update the required operator documentation in `Wsprry_Pi_Docs`.
16. Render the authorized documentation changes using that repository's workflow and review affected rendered HTML with Impeccable; replace screenshots only if materially inaccurate.
17. If documentation writes are not authorized, report the required `Wsprry_Pi_Docs` follow-up instead.
18. Review complete UI submodule, parent, and authorized documentation diffs separately.
19. Report remaining hardware or runtime qualification separately.
20. Commit or push only if explicitly requested for each repository boundary.

## Implementation Gate

This document is an implementation contract, not authorization to modify code.

Before implementation:

1. Inspect the current parent repository and recursive submodule state.
2. Confirm the exact `WsprryPi-UI` revision.
3. Confirm the exact timing-key inventory.
4. Confirm current mode-specific runtime construction.
5. Confirm that Impeccable is available.
6. Compare current behavior with this document.
7. Report any remaining material drift or ambiguity.
8. Obtain explicit approval to implement application and UI changes.
9. Obtain explicit cross-repository authorization to modify `Wsprry_Pi_Docs`, or define the documentation deliverable as a follow-up report only.
