 # WsprryPi Option Consistency Triage

## Confirmed bug

~~### T01 UI frequency validator rejects backend-valid WSPR frequency forms~~

~~- Brief: The web UI rejects values the backend accepts, including 0 skip windows, comma-separated lists, @GPIO[H|L] suffixes, and aliases such as 2200m, 630m, 22m, 2200m-15, 630m-15.~~
~~- Why this bucket: The code proves the parser accepts these forms while the frontend va~~lidator blocks them.~~
~~- Authoritative surface: Backend parser/runtime semantics in src/arg_parser.cpp:2125 and band lookup in src/wspr_band_lookup.cpp:359.~~
~~- Real runtime behavior difference: Yes. The UI prevents valid persisted/runtime configurations from being entered or re-saved.~~
~~- User confusion only: No.~~
~~- Severity: high~~
~~- Confidence: high~~
~~- Evidence: WsprryPi-UI/data/index.js:1735, src/arg_parser.cpp:585, src/wspr_band_lookup.cpp:131~~

### T02 UI CW base-frequency units are validated as valid, then serialized incorrectly

- Brief: The UI accepts unit-suffixed CW base frequency input like 7.0401MHz, but buildConfigPayload() uses parseFloat, sending 7.0401 instead of Hz.
- Why this bucket: This is a direct frontend serialization bug, not a policy difference.
- Authoritative surface: Backend/API numeric CW.Base Frequency semantics; the UI is supposed to serialize into that contract.
- Real runtime behavior difference: Yes. It can produce the wrong transmitted frequency.
- User confusion only: No.
- Severity: high
- Confidence: high
- Evidence: WsprryPi-UI/data/index.js:1790, WsprryPi-UI/data/index.js:1472

### T03 UI fallback default for GPIO.Use NTP disagrees with backend default

- Brief: Backend defaults missing GPIO.Use NTP to true, while the UI loader falls back to false.
- Why this bucket: Missing-key configs can load differently in UI and then be re-saved with changed behavior.
- Authoritative surface: Backend config normalization in src/config_handler.cpp:1157.
- Real runtime behavior difference: Yes. A partial or older config can change calibration mode after a UI save.
- User confusion only: Not only confusion.
- Severity: medium
- Confidence: medium
- Evidence: src/config_handler.cpp:1063, src/config_handler.cpp:1157, WsprryPi-UI/data/site.js:1009

## Intentional asymmetry

### T04 CLI only exposes require_paired, not full planner-preference enum

- Brief: UI/INI support auto, prefer_paired, require_paired; CLI only adds --require-paired.
- Why this bucket: The CLI option shape looks deliberately minimal rather than accidentally incomplete.
- Authoritative surface: Persistent/public setting is WSPR.Planner Preference; CLI is a narrower override surface.
- Real runtime behavior difference: Yes, but only because CLI intentionally cannot express one persisted state.
- User confusion only: Mostly, yes.
- Severity: low
- Confidence: medium
- Evidence: src/arg_parser.cpp:2444, src/config_handler.cpp:78, WsprryPi-UI/data/views/config.php:258

### T05 CLI one-shot and test-mode controls are not mirrored in UI/INI

- Brief: --repeat, --terminate, --test-tone, and transient --qrss-* / --fskcw-* / --dfcw-* are CLI-oriented runtime controls, not full cross-surface settings.
- Why this bucket: The code treats these as transient startup/runtime requests, not durable config.
- Authoritative surface: CLI for those flows.
- Real runtime behavior difference: Yes, but by design.
- User confusion only: Low.
- Severity: low
- Confidence: high
- Evidence: src/arg_parser.hpp:7, src/arg_parser.cpp:2683, src/arg_parser.cpp:2874

### T06 Operation.Mode=TONE is internal/transient, not a normal persisted/public mode

- Brief: Internal code can parse TONE, but normal serialization maps runtime tone mode back to WSPR, and the UI does not expose TONE.
- Why this bucket: The code clearly treats tone mode as transient runtime behavior.
- Authoritative surface: Internal scheduler/runtime only.
- Real runtime behavior difference: No mismatch bug in normal user config flow.
- User confusion only: Possibly, if someone patches API manually.
- Severity: low
- Confidence: high
- Evidence: src/config_handler.cpp:224, src/config_handler.cpp:1331, src/scheduling.hpp:170

## Documentation/help-text gap

### T07 CLI help text omits many supported options

- Brief: --help does not enumerate several supported options and aliases, including --use-ntp, --repeat, --offset, --journald, --date-time-log, --require-paired, --terminate, and the
non-WSPR options list.
- Why this bucket: The parser supports them; the help text is the lagging surface.
- Authoritative surface: CLI parser in src/arg_parser.cpp:2433.
- Real runtime behavior difference: No.
- User confusion only: Yes.
- Severity: medium
- Confidence: high
- Evidence: src/arg_parser.cpp:1055, src/arg_parser.cpp:2433

## Hidden advanced option

### T08 Si5351.TX Output is persisted/public but intentionally not exposed in normal UI

- Brief: The field exists in API/INI/CLI, but the web UI does not render a control for it.
- Why this bucket: Stock INI comments explicitly say it is configurable in INI and CLI and not exposed in the web UI.
- Authoritative surface: INI/CLI/API, with UI intentionally narrower.
- Real runtime behavior difference: Only if the user needs to edit it from UI.
- User confusion only: Mostly.
- Severity: medium
- Confidence: high
- Evidence: config/wsprrypi.ini:94, src/config_handler.cpp:330, src/tests/dial_frequency_semantics_test.cpp:2048

### T09 Operation.Web Port and Operation.Socket Port are API/INI/CLI options but hidden in UI

- Authoritative surface: INI/CLI/API.
- Real runtime behavior difference: No direct mismatch unless user expects UI editing.
- User confusion only: Yes.
- Severity: low
- Confidence: high
- Evidence: WsprryPi-UI/data/site.js:1071, src/config_handler.cpp:378, config/wsprrypi.ini:36

### T10 Advanced CW envelope/gap settings are persisted and consumed, but hidden in UI

- Brief: CW.Intra Element Gap, Inter Character Gap, Inter Word Gap, Fade Shape, Fade In/Out/Slice Ms are runtime-relevant and persisted, but not shown on the normal config page.
- Why this bucket: The code fully supports them, and omission from UI looks like a deliberate simplification of the normal surface.
- Authoritative surface: INI/API/backend.
- Real runtime behavior difference: Yes, these affect runtime, but the mismatch is exposure, not interpretation.
- User confusion only: Mostly.
- Severity: medium
- Confidence: medium
- Evidence: src/config_handler.cpp:1188, src/config_handler.cpp:1389, src/scheduling.cpp:1524

### T11 Meta.debug_logging is writable internally/API-side but hidden from public GET/UI

- Brief: Public config omits Meta, but PATCH logic still accepts Meta.debug_logging and persists it.
- Why this bucket: Tests explicitly assert that public config should hide Meta.
- Authoritative surface: Internal/API patch path and INI.
- Real runtime behavior difference: Yes if manually patched, but notI’m re-triaging the first-pass findings conservatively against the code paths and test evidence. I’ll keep anything
without strong proof of intent in the ambiguous bucket instead of inferring design intent from comments alone.

• # WsprryPi Option Consistency Triage

## Confirmed bug

### F01 UI WSPR frequency validator rejects backend-valid inputs

- Brief: The web UI rejects valid backend frequency forms such as 0, comma-separated lists, @GPIO[H|L] suffixes, and accepted aliases like 2200m, 630m, 22m, 2200m-15, 630m-15.
- Why this bucket: The code proves the backend accepts these forms while the UI validator blocks them.
- Authoritative surface: Backend parser/runtime, especially src/arg_parser.cpp:569, src/arg_parser.cpp:585, src/wspr_band_lookup.cpp:131, src/wspr_band_lookup.cpp:359.
- Real runtime behavior difference: Yes.
- User confusion only: No.
- Severity: high
- Confidence: high

### F02 UI CW base-frequency field accepts units but saves the wrong numeric value

- Brief: The UI validates 7.0401MHz as valid, but buildConfigPayload() uses parseFloat, so it sends 7.0401.
- Why this bucket: This is a concrete serialization bug in the UI.
- Authoritative surface: Backend numeric semantics, with UI serializer at WsprryPi-UI/data/index.js:1472 contradicting UI validation at WsprryPi-UI/data/index.js:1806.
- Real runtime behavior difference: Yes.
- User confusion only: No.
- Severity: high
- Confidence: high

### F12 UI fallback default for GPIO.Use NTP differs from backend default

- Brief: Backend default/missing-key behavior is true, but frontend load fallback uses false.
- Why this bucket: The mismatch can change saved config if the key is missing in older or partial configs.
- Authoritative surface: Backend config model in src/config_handler.cpp:1063, src/config_handler.cpp:1157.
- Real runtime behavior difference: Yes, on partial/missing-key configs.
- User confusion only: No.
- Severity: medium
- Confidence: medium

### F13 Raw HTML default for CW shift is 0 while config defaults are 500

- Brief: UI markup starts with 0, but internal and stock config defaults are 500.
- Why this bucket: This can produce a wrong saved value before backend load completes or in degraded/offline states.
- Authoritative surface: Persistent config defaults in src/config_handler.cpp:1090, config/wsprrypi.ini:154.
- Real runtime behavior difference: Yes, in pre-load/offline/degraded UI flows.
- User confusion only: No.
- Severity: medium
- Confidence: medium

## Intentional asymmetry

### F17 Internal TONE mode is not a normal public/persisted UI mode

- Brief: Internal parser accepts TONE, but serializer writes runtime tone back as WSPR and UI does not expose TONE.
- Why this bucket: The code treats tone as transient runtime behavior, not durable config.
- Authoritative surface: Scheduler/runtime model in src/config_handler.cpp:1331, src/scheduling.cpp:2744.
- Real runtime behavior difference: No, this is a boundary choice.
- User confusion only: Possible if someone patches raw API manually.
- Severity: low
- Confidence: high

## Documentation/help-text gap

### F16 CLI help text is incomplete relative to supported options

- Brief: --help omits many supported options and aliases.
- Why this bucket: The parser supports them; the mismatch is in help/discoverability.
- Authoritative surface: CLI parser in src/arg_parser.cpp:2433, not the help text in src/arg_parser.cpp:1055.
- Real runtime behavior difference: No.
- User confusion only: Yes.
- Severity: medium
- Confidence: high

## Hidden advanced option

### F04 Si5351.TX Output is public/persisted but not editable in the normal UI

- Brief: CLI, API, and INI support TX Output, but the UI does not expose a control.
- Why this bucket: The INI comment explicitly says it is configurable in INI/CLI and not exposed in the web UI.
- Authoritative surface: INI/API/backend config in config/wsprrypi.ini:94, src/config_handler.cpp:1355.
- Real runtime behavior difference: Yes, but only if changed via CLI/API/INI.
- User confusion only: Usually no for advanced users; maybe yes for UI-only users.
- Severity: low
- Confidence: high

### F05 Operation.Web Port and Operation.Socket Port are persisted/API fields but hidden in UI

- Brief: CLI/API/INI expose web/socket ports; config page does not.
- Why this bucket: Frontend code explicitly calls them backend-managed and omits visible controls.
- Authoritative surface: Backend config in src/config_handler.cpp:378, with UI comment at WsprryPi-UI/data/site.js:1071.
- Real runtime behavior difference: Yes, but only when edited through non-UI surfaces.
- User confusion only: Low.
- Severity: low
- Confidence: high

### F06 Advanced CW.* envelope/gap controls are persisted and used, but hidden in UI

- Brief: Intra/Inter gaps and fade settings are loaded, validated, persisted, and consumed at runtime, but not surfaced on the config page.
- Why this bucket: The code clearly supports them as advanced persisted settings without UI editors.
- Authoritative surface: Backend config/runtime in src/config_handler.cpp:1188, src/config_handler.cpp:1389, src/scheduling.cpp:1524.
- Real runtime behavior difference: Yes, if set through INI/API.
- User confusion only: Mostly yes for UI-only users.
- Severity: low
- Confidence: high

### F08 Meta.debug_logging is writable internally/API-side but hidden from public config GET

- Brief: PATCH can apply it, INI persists it, but public GET hides Meta.
- Why this bucket: Tests explicitly assert that public config should not expose Meta.
- Authoritative surface: Internal config/INI, with hiding enforced by src/config_handler.cpp:295 and tested in src/tests/dial_frequency_semantics_test.cpp:1968.
- Real runtime behavior difference: Yes, if changed through internal/API path.
- User confusion only: Mostly yes.
- Severity: low
- Confidence: high

## Ambiguous/policy decision needed

### F03 Planner preference is 3-state in UI/INI but CLI only has --require-paired

- Brief: CLI cannot express prefer_paired.
- Why this bucket: This could be deliberate CLI simplification or an omission; code does not prove intent.
- Authoritative surface: No single clear authority; UI/INI model is richer, CLI is narrower.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high

### F07 WSPR.WSPR Dial Frequency Set is internal/API-visible but not UI-visible or INI-persisted

- Brief: The field exists in internal/public JSON flow, is read if present, but is not shown in UI and is not persisted to INI.
- Why this bucket: It may be an internal cache/normalized field rather than a user option, but the public JSON exposure muddies that.
- Authoritative surface: Unclear.
- Real runtime behavior difference: Possibly, depending on entry path.
- User confusion only: Yes.
- Severity: low
- Confidence: medium

### F09 Band-GPIO fallback differs by entry path

- Brief: INI/web configs allow band-GPIO fallback for entries without @GPIO; CLI non-INI entries do not.
- Why this bucket: Tests prove the difference exists, but not whether it is intended policy.
- Authoritative surface: Unclear; behavior depends on entry path by design, but intent is not explicit enough.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high

### F10 CW base-frequency defaults diverge across internal defaults, stock INI, and raw HTML

- Brief: Internal default is 3572000, stock INI is 14096900, raw HTML is 7040000.
- Why this bucket: The divergence is proven, but the correct authority is not.
- Authoritative surface: Unclear.
- Real runtime behavior difference: Yes, in missing-key/repair/offline/degraded flows.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high

### F11 Band GPIO defaults diverge between internal defaults and stock INI/UI

- Brief: Internal config seeds many enabled assignments; stock INI leaves them blank; UI markup starts empty.
- Why this bucket: The mismatch is real, but the intended source of truth is unclear.
- Authoritative surface: Unclear.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high

### F14 PPM range enforcement differs by entry path

- Brief: CLI and UI constrain around [-200, 200]; backend validation and INI/API do not.
- Why this bucket: Could be deliberate UX guardrails rather than a bug, but code does not make that policy explicit.
- Authoritative surface: Backend accepts broader values; UI/CLI are narrower.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high

### F15 CW numeric range limits differ between UI and backend

- Brief: UI caps dot/repeat/shift more tightly than backend, which mostly requires positive values plus repeat policy.
- Why this bucket: Could be intentional UI guardrails, but not proven.
- Authoritative surface: Backend runtime/validation.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high

## Bugs worth fixing first

- F01 UI WSPR frequency validator is narrower than backend parsing and blocks valid values.
- F02 UI CW base-frequency unit handling corrupts saved values.
- F12 UI fallback for GPIO.Use NTP disagrees with backend default on missing-key configs.
- F13 UI raw default for CW shift disagrees with config defaults and can save the wrong value before backend state loads.

## Differences that should remain unchanged for now

- F04 Si5351.TX Output hidden from normal UI.
- F05 Web Port and Socket Port hidden from normal UI.
- F06 advanced CW.* envelope/gap controls hidden from normal UI.
- F08 Meta.debug_logging hidden from public config GET.
- F17 transient internal TONE mode not exposed as a normal persisted UI mode.
