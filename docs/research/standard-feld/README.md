# Standard Feld implementation-design and qualification-planning spike

## Status

**Phase 5 Slice 3 complete — the compiled plan now has a validated GPIO-backend-adjacent non-transmitting dry-run seam; production backend execution, operator selection, hardware, RF, operator, and release gates remain open.**

This record continues the completed generic [Hellschreiber research spike](../hellschreiber/README.md) for Standard Feld-Hell alone. It records the frozen product protocol, the separately authorized compiler, internal parent, and non-transmitting GPIO dry-run slices, and later qualification work without authorizing production backend execution, hardware operation, RF output, operator exposure, or release.

## Scope

- Research, specification, architecture, bounded compiler and internal parent-source implementation, non-transmitting backend interpretation, and qualification planning.
- Standard Feld-Hell is the sole candidate.
- The generic mode-selection question remains closed unless material new evidence invalidates the prior record.
- fldigi 4.2.12 `FSKH105`, xfhell 3.5.2 `FMHell105`, the documentary six-row profile, Hell-80, GL-Hell, and private variants are excluded.
- The transmitter submodule contains the bounded production asset/compiler and a test-only GPIO dry-run interpreter. The parent contains an internal, execution-suppressed Standard Feld model plus a focused dry-run test target. CLI, INI, JSON, web, WebSocket, UI, production backend execution, operator documentation, services, hardware, audio devices, and RF behavior remain unavailable or unchanged.

## Baseline

| Repository | Branch inspected | Commit | Phase 1.1 use |
| --- | --- | --- | --- |
| `WsprryPi/WsprryPi` | `research/hellschreiber-review` | `708bace20374a367f54801f1a87ff814f5b1f117` | Completed generic research baseline; new work is on `research/standard-feld-design` |
| `WsprryPi/hellschreiber-interoperability-rig` | `research/standard-feld-contract` | `088bbaecb1afe0f55be13cfb90f1496c031a24fb` | Committed Phase 3 exact-asset adapter, protocol, and result record |

At Phase 3 closeout, both repositories were clean and even with their upstreams. All WsprryPi submodules were initialized, clean, detached at their recorded commits, and neither dirty nor mismatched. The rig remained read-only during product-record reconciliation.

## Phase path

1. **Phase 1.1:** freeze the font-independent protocol core and fixture schema.
2. **Phase 2:** select one immutable redistributable font, repertoire, and spacing policy.
3. **Phase 1.2:** generate exact-asset raster, event, duration, and cancellation fixtures.
4. **Phase 3:** qualify the exact contract in the contained interoperability rig.
5. **Phase 4:** design architecture and product behavior.
6. **Phase 5:** implement separately authorized, bounded source slices; Slice 1 supplies the immutable asset/compiler, Slice 2 supplies the execution-suppressed parent seam, and Slice 3 supplies non-transmitting GPIO plan interpretation.
7. **Phases 6–7:** define and execute separately authorized backend, spectral, RF, operator, and release qualification.
8. **Phase 8:** make a bounded product go/no-go decision, distinct from hardware, RF, and release readiness.

The ordering intentionally places asset selection before glyph-bearing fixtures. A protocol-mechanics fixture can be font-independent; a production character fixture cannot.

## Documents

- [Normative protocol](protocol.md)
- [Font selection](font.md)
- [Spacing and input policy](spacing-policy.md)
- [Selected asset](assets/README.md)
- [Fixture contract](fixtures/README.md)
- [Phase 1.2 fixture report](phase-1.2-fixtures.md)
- [Phase 3 offline qualification](phase-3-offline-qualification.md)
- [Phase 4 product architecture](phase-4-architecture.md)
- [Phase 5 Slice 1 production compiler](phase-5-slice-1-compiler.md)
- [Phase 5 Slice 2 parent integration](phase-5-slice-2-parent-integration.md)
- [Phase 5 Slice 3 GPIO dry-run](phase-5-slice-3-gpio-dry-run.md)
- [Research tools](tools/README.md)
- [Decision log](decision-log.md)
- [Evidence register](evidence-register.md)

## Readiness boundary

| Readiness class | Current result |
| --- | --- |
| Protocol-core design | Frozen for Phase 1.1 and reconciled to Phase 2 asset |
| Gate A protocol contract | Satisfied: exact fixtures independently reproduced |
| Font and licensing | Gate B satisfied: immutable MIT asset and spacing policy frozen |
| Gate C exact-asset interoperability | Partially satisfied: corrected exact-corpus clean F3 passed in both named receivers; exact-contract direction coverage, repertoire coverage, F4 feasibility, and independent scoring remain open |
| Phase 4 architecture design | Passed: first-class raster payload/compiler over the shared execution plan; no new hardware backend |
| Phase 5 Slice 1 | Passed: immutable production asset and backend-neutral compiler implemented and validated |
| Phase 5 Slice 2 | Passed: internal parent validation, request, scheduling, cancellation, and source status integrated under test execution suppression |
| Phase 5 Slice 3 | Passed: exact test-only GPIO plan interpretation and terminal safe-idle intent validated without hardware enablement |
| Operator-facing parent integration | Not implemented: CLI, INI, JSON, web, WebSocket, and UI remain unavailable |
| Production backend execution | Disabled: Raspberry Pi acceptance guard unchanged; Si5351 explicitly rejects Standard Feld |
| Regression debt | `guarded-mode-change-persistence-test` has a baseline-reproduced pre-Slice-2 QRSS empty-message failure |
| Standalone transmitter build | Pre-existing parent-header include-boundary limitation remains documented |
| Hardware qualification | Not assessed |
| RF/spectral qualification | Not assessed |
| Release readiness | Not ready |

## Phase 1.1 conclusion

`PHASE 1.1 PASS — PROTOCOL CORE FROZEN; GATE A REMAINS OPEN PENDING ASSET-BOUND FIXTURES`

## Phase 2 conclusion

`PHASE 2 PASS — IMMUTABLE FONT AND SPACING POLICY FROZEN; GATE B SATISFIED; GATE A REMAINS OPEN PENDING PHASE 1.2 FIXTURES`

## Phase 1.2 conclusion

`PHASE 1.2 PASS — EXACT-ASSET FIXTURES FROZEN; GATE A SATISFIED`

## Phase 3 conclusion

`PHASE 3 PASS — EXACT-CORPUS OFFLINE RECEIVE F3 QUALIFIED THROUGH THE PINNED APPLICATION ADAPTER; GATE C PARTIAL; INDEPENDENT REVIEW PENDING`

## Phase 4 conclusion

`PHASE 4 PASS — ARCHITECTURE SELECTED; FIRST IMPLEMENTATION SLICE MAY BE PROPOSED FOR SEPARATE AUTHORIZATION; GATES C–G REMAIN OPEN AS RECORDED`

## Phase 5 Slice 1 conclusion

`PHASE 5 SLICE 1 PASS — IMMUTABLE PRODUCTION ASSET AND BACKEND-NEUTRAL STANDARD FELD COMPILER IMPLEMENTED AND VALIDATED`

## Phase 5 Slice 2 conclusion

`PHASE 5 SLICE 2 PASS — PARENT STANDARD FELD MODE, VALIDATION, REQUEST, SCHEDULING, CANCELLATION, AND SOURCE STATUS INTEGRATED WITHOUT BACKEND ENABLEMENT`

## Phase 5 Slice 3 conclusion

`PHASE 5 SLICE 3 PASS — GPIO BACKEND STANDARD FELD PLAN ACCEPTANCE AND NON-TRANSMITTING DRY-RUN TRACE IMPLEMENTED WITHOUT HARDWARE ENABLEMENT`
