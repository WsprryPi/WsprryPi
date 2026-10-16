# Item 13 retained visual and detector evidence

See the [review record](../item13-integrated-impeccable-review.md) for candidate
identity, methodology, findings, exact commands, CI and limits. All data shown
are synthetic. These are sampled browser captures, not physical-device,
screen-reader or RF qualification.

Chrome 155.0.8059.40: desktop 1440×1000, mobile 390×844; light/dark. Filenames
containing `200` use 200% root text sizing, not native browser zoom. The menu
captures deliberately show the scrolled lower navigation and focused theme
control. The keyboard-return capture shows focus restored after Test Tone
Space/Escape; no Start action runs. Modal lifecycle screenshots use mocked
transport and committed-response fixtures.

| Files | Evidence |
| --- | --- |
| `operation-light-desktop.png`, `operation-dark-mobile.png` | Console hierarchy; mobile runtime controls before reboot choices |
| `config-dark-desktop.png`, `config-DFCW-dark-mobile.png` | Setup navigation contrast, mode-specific controls |
| `logs-light-desktop.png`, `logs-dark-mobile-200-controls.png` | Populated production SSE handler, first-row clearance, enlarged nonoverlapping controls |
| `spots-light-desktop.png`, `spots-dark-mobile-200-percent.png` | Demo table and enlarged shell/clock wrapping |
| `maintenance-light-desktop.png`, `maintenance-about-dark-mobile.png` | Recovery hierarchy and viewport-bounded About panel |
| `maintenance-menu-light-mobile-200-percent.png`, `maintenance-menu-dark-mobile-200-percent.png` | Expanded menu can scroll to the keyboard-focused theme control in both themes |
| `maintenance-keyboard-return-dark-mobile.png` | Dismissed modal returns visible keyboard focus to Test Tone |
| `tone-dark-mobile-recovery.png`, `tone-light-desktop-active-difference.png` | Recovery and committed-vs-preview Test Tone feedback |

`layout-measurements.json` retains 20 normal page/theme/viewport samples,
computed enabled-button/tab contrast and description/width checks. Additional
200% control/text-range, keyboard, hover/active/focus and reachability assertions
live in the committed browser test; not every assertion is serialized here.

`detector-command.json` records the exact 44-source invocation and exit 2;
`detector-source.json` holds the final three developer-console color false
positives. `detector-browser-counts.json` is Assessment B's initial ten-view
raw count, including repeated/overlay detections; it is not a final defect count.
The record explains each disposition and unavailable capability.

`reviewed-inputs.json` hashes the tracked UI component and non-hardware CI
workflow plus the parent hardware-access audit and its regression at the reviewed
implementation. `initial-ci.json` retains the all-green initial run, whose final
audit is explicitly rejected as proof because its missing trace inputs were
ignored (F13); `initial-audit-failure.txt` retains the exact error/success
contradiction. Corrected CI is required. It excludes generated/ignored runtime
files. Later evidence/checklist-only commits must preserve these inputs.

`implementation-ci.json` retains the corrected exact-candidate run38952419761,
with all 11 job conclusions. `corrected-audit.txt` retains the five-test pass and
actual nonempty five-trace audit output. The final evidence/checklist-only head's
own CI and remote identity are linked from #423's closing evidence comment.
