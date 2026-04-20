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

### F15 CW numeric range limits differ between UI and backend

- Brief: UI caps dot/repeat/shift more tightly than backend, which mostly requires positive values plus repeat policy.
- Why this bucket: Could be intentional UI guardrails, but not proven.
- Authoritative surface: Backend runtime/validation.
- Real runtime behavior difference: Yes.
- User confusion only: Also yes.
- Severity: medium
- Confidence: high
