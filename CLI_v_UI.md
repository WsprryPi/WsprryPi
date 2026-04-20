 # WsprryPi Option Consistency Triage

### T10 Advanced CW envelope/gap settings are persisted and consumed, but hidden in UI

- Brief: CW.Intra Element Gap, Inter Character Gap, Inter Word Gap, Fade Shape, Fade In/Out/Slice Ms are runtime-relevant and persisted, but not shown on the normal config page.
- Why this bucket: The code fully supports them, and omission from UI looks like a deliberate simplification of the normal surface.
- Authoritative surface: INI/API/backend.
- Real runtime behavior difference: Yes, these affect runtime, but the mismatch is exposure, not interpretation.
- User confusion only: Mostly.
- Severity: medium
- Confidence: medium
- Evidence: src/config_handler.cpp:1188, src/config_handler.cpp:1389, src/scheduling.cpp:1524

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

### F15 CW numeric range limits differ between UI and backend

- Brief: UI caps dot/repeat/shift more tightly than backend, which mostly requires positive values plus repeat policy.
- Why this bucket: Could be intentional UI guardrails, but not proven.
- Authoritative surface: Backend runtime/validation.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high
