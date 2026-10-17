# Item 15 — P15.9 originating post-execution acceptance

Date: 2026-10-17. Coordinating repository: WsprryPi. Implementation: WsprryPico.

## Outcome and source identities

Item 15 is closed within the supported Pico software and originating-assessment
scope. The originating assessment reproduced and repaired one P1 and three P2
findings. A fresh independent reassessment found no unresolved actionable finding
in the repaired software. The separately prepared closure documentation was also
reviewed for evidence accuracy and stale pending statements.

| Repository or boundary | Reviewed state |
| --- | --- |
| Pico starting `devel` | `156dfa16fb73cb599c96409388c34fde4cfa594a`, matching upstream and independently queried remote |
| Pico original product / execution closeout | `56821ac827c901b61f3af83887eb8c447520abb5` / `29542e40e131b3e0faa5be5522d220f906de4b0f` |
| Subsequent Pico P15.12 integration | `227e73c71c0bbda7e1d84565d7d520e0d7a3f1a0`, included in the reviewed starting head |
| Pico repaired implementation and originating record | `165303458e49feaac1de54c8344058f56578d533`, pushed to `origin/devel` without force; local, upstream and independent remote hashes match; working tree clean |
| Pi software candidate, unchanged during this review | `a84010d6c16ac416d52989053ac1f273390a8ab5` |
| Pi current Pico interoperability pin, unchanged | P15.8 `804e8a06585ec5a0e650e69fcd71ada398b6cc9f` |

The [immutable Pico originating record](https://github.com/WsprryPi/WsprryPico/blob/165303458e49feaac1de54c8344058f56578d533/docs/development/phase15-9-post-execution-review.md)
contains the exact nine reviewed product/test input hashes, reproduction and
repair history, dependency identities, commands, failures, limits and results.
The [original execution evidence](https://github.com/WsprryPi/WsprryPico/blob/29542e40e131b3e0faa5be5522d220f906de4b0f/docs/development/phase15-9-valid-field-saves.md)
remains historical and source-bound. Neither a checklist mark nor Pages
publication was used as evidence that software requirements had been met.

## Findings and final reassessment

| Severity | Confirmed issue | Repair and meaningful regression | Final state |
| --- | --- | --- | --- |
| P1 | A failed progress append made storage unhealthy but still admitted qualified-band direct WTP preparation. | Recheck health at prepare, schedule and begin. A real failed-journal/API test rejects all five modes without reaching the raw engine, including a fault after preparation. Applied configuration, watermark and cleanup remain intact. | Closed |
| P2 | A delayed rejected or cleared field reply could claim the earlier value was saved after a newer edit. | Preserve the real earlier outcome beside the newer-unsaved qualification on both pages. Hold actual engineering/public AEAD replies; verify text, retained edits and invalid/clear distinctions. | Closed |
| P2 | Native authenticated consumer power selection could not explicitly clear an existing value. | Provide an empty choice; verify null persistence, blocked incomplete application, no applied-profile/config/restart side effect and valid explicit zero. | Closed |
| P2 | Public incomplete-progress recovery omitted the present authoritative saved tuple while preserving different browser edits. | Always show saved values and explicit absence. After a lost reply, a second actual public browser saves BB1BB while the first retains DD3DD; read/review shows both without replay or request attribution. | Closed |

Independent backend/browser reviewers examined the original current state. A
fresh independent reviewer then challenged the repaired final product inputs,
authority boundaries, failure/recovery paths and regressions. It independently
reran all 43 shared JS assertions and whitespace checks; no actionable finding
remained. Its source review is separate from the coordinating executor's compiled,
real-browser, sanitizer and firmware-build evidence. The final closure record
and concurrent-browser regression were additionally reassessed for accuracy.

## Validation and boundaries

- Full Pico host suite: **200 executed groups passed**, 201 registered; optional
  `config_update_schema` skipped because Python `jsonschema` is unavailable.
  The final full run used `source scripts/xcode_env.sh`. A prior invocation
  omitted that setup and failed three groups on the known CLT TAPI/linker
  mismatch; its corrected result and failed attempt are retained.
- Affected ASan/UBSan: **13/13 groups passed**. LeakSanitizer is unsupported on
  this macOS host and disabled; no full-program leak claim is made.
- JS/browser: **43 shared assertions**, existing network browser checks and
  **17/17 bootstrap Node groups** passed. Production-schema samples passed
  **44 checks**, and three-client progress/session coverage passed **35 assertions**.
- Genuine production served-page, crypto, commissioning and carrier workflows
  passed. Board/TCP and engineering mTLS boundaries remain explicit doubles.
  Call-only saves beside blank QRSS, mixed outcomes, null/zero, explicit apply,
  journal/runtime reconstruction, lost/future/delayed replies and concurrent
  public-browser recovery were exercised.
- Both Standard and StandaloneRF firmware images compiled and linked using
  retained pinned dependencies and Arm GCC 15.3.1. Text/BSS bytes were
  2,192,032/174,000 and 2,207,492/329,940. Flash ends `0x1020eaa4` and
  `0x10212704` remain below `0x103f0000`; static heap spans remain 301,424 and
  145,484 bytes. Build-time guards passed; these are static results.
- Impeccable 4.1.1 hardening/context/craft guidance was used. The final genuine
  Chrome run captured **41 desktop/mobile/200% CSS-zoom images** under both OS
  preferences. Representative error, recovery, mixed and complete states were
  inspected. Keyboard/focus and horizontal-overflow assertions passed; the
  authored-JS detector returned `[]`. This is the existing light palette,
  not a separately implemented dark theme or accessibility certification.
- Local links, nine final input hashes and final whitespace checks passed.
  Local execution logs and visual evidence are retained under
  `/private/tmp/wsprrypi-p159-postreview`.

Pico's exact repaired head `1653034` passed all three
[Pages publication checks](https://github.com/WsprryPi/WsprryPico/actions/runs/39026250793).
These build/deploy/report checks are not a firmware/software-test pipeline;
no firmware-test CI pass is claimed. Pi's exact pushed-head
CI likewise does not advance its unchanged Pico pin or validate adoption of the
new saved-progress operations.

The historical unchanged Pi compatibility pairs were against exact Pico product
`56821ac`, using Pi `a84010d` and accepted companion `6d5e734`. They were not
rerun against these repairs or used as new evidence for the later P15.12 source.
No Pi C++ compilation/linking ran locally on macOS. No Pi application/component,
UI, test, fixture, workflow, pin or compatibility row changed.

## Documentation Impact and remaining decisions

Updated in Pico: the originating review and four preserved closure documents
(development index, original technical handoff status, execution record and
implementation plan). Existing saved-progress contracts already require the
repaired behavior and retain their schema/authority semantics.

Updated in Pi: the release punchlist, this coordinating record and saved P15.9
prompt metadata. Its original fenced operator prompt remains byte-for-byte
unchanged. The user's existing item 14 acceptance details and item 15 closure
edit were preserved. These Pi changes are documentation only.

Considered unchanged: Pi product code, reusable components, Type 2/3 acceptance,
Pico P15.12 presentation beyond the affected saved-progress feedback, complete
atomic APIs, journal format, flash topology, licensing and operator Docs. The
nonblocking P15.8 explanation-wording follow-up remains separate.

Still required separately before any affected Beta freeze: explicitly select
or defer Pi/Beta adoption and the applicable documentation inputs. The Pi member
editor remains its complete atomic editor; this assessment does not adopt Pico
saved-progress operations or advance the interoperability pin. P15.14 operator
documentation/screenshots and P15.13 physical qualification remain separate,
with exact follow-ups in the Pico execution record. Any future pin advance must
preserve all historical rows and retain P15.8 as `p158` (nine rows total).

The originating assessment is now complete; it is no longer an outstanding gate.
Item 16 remains not started and stops after 16c under the existing authorization.
No hardware, flashing, installed-service, installation, GPIO, timing or RF
qualification occurred. No release, tag, PR, operator-doc write/publication,
issue announcement or message to another conversation was performed.
