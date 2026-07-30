# Standard Feld implementation-design and qualification-planning spike

## Status

**Phase 1.2 complete — exact-asset fixtures frozen; Gates A and B satisfied; Gate C remains open.**

This record continues the completed generic [Hellschreiber research spike](../hellschreiber/README.md) for Standard Feld-Hell alone. It defines a proposed product protocol and later qualification work without authorizing implementation, hardware operation, RF output, or release.

## Scope

- Research, specification, architecture design, and qualification planning only.
- Standard Feld-Hell is the sole candidate.
- The generic mode-selection question remains closed unless material new evidence invalidates the prior record.
- fldigi 4.2.12 `FSKH105`, xfhell 3.5.2 `FMHell105`, the documentary six-row profile, Hell-80, GL-Hell, and private variants are excluded.
- Application code, tests, configuration, CLI, scheduling, persistence, UI, submodules, operator documentation, dependencies, services, hardware, audio devices, and RF behavior remain unchanged.

## Baseline

| Repository | Branch inspected | Commit | Phase 1.1 use |
| --- | --- | --- | --- |
| `WsprryPi/WsprryPi` | `research/hellschreiber-review` | `708bace20374a367f54801f1a87ff814f5b1f117` | Completed generic research baseline; new work is on `research/standard-feld-design` |
| `WsprryPi/hellschreiber-interoperability-rig` | `main` | `9e37756758e1d59eeb7b3b8a01dd477a66dd9309` | Read-only reproducibility and fixture-consumer baseline |

Both repositories were clean and even with their upstreams at inspection. All WsprryPi submodules were initialized, clean, detached at their recorded commits, and neither dirty nor mismatched. The rig was not changed or executed.

## Phase path

1. **Phase 1.1:** freeze the font-independent protocol core and fixture schema.
2. **Phase 2:** select one immutable redistributable font, repertoire, and spacing policy.
3. **Phase 1.2:** generate exact-asset raster, event, duration, and cancellation fixtures.
4. **Phase 3:** qualify the exact contract in the contained interoperability rig.
5. **Phases 4–7:** design architecture and product behavior, then define backend, spectral, RF, operator, and release qualification.
6. **Phase 8:** make a bounded implementation go/no-go decision, distinct from hardware, RF, and release readiness.

The ordering intentionally places asset selection before glyph-bearing fixtures. A protocol-mechanics fixture can be font-independent; a production character fixture cannot.

## Documents

- [Normative protocol](protocol.md)
- [Font selection](font.md)
- [Spacing and input policy](spacing-policy.md)
- [Selected asset](assets/README.md)
- [Fixture contract](fixtures/README.md)
- [Phase 1.2 fixture report](phase-1.2-fixtures.md)
- [Research tools](tools/README.md)
- [Decision log](decision-log.md)
- [Evidence register](evidence-register.md)

## Readiness boundary

| Readiness class | Current result |
| --- | --- |
| Protocol-core design | Frozen for Phase 1.1 and reconciled to Phase 2 asset |
| Gate A protocol contract | Satisfied: exact fixtures independently reproduced |
| Font and licensing | Gate B satisfied: immutable MIT asset and spacing policy frozen |
| Exact-asset interoperability | Not run |
| Production implementation | Not authorized |
| Hardware qualification | Not assessed |
| RF/spectral qualification | Not assessed |
| Release readiness | Not ready |

## Phase 1.1 conclusion

`PHASE 1.1 PASS — PROTOCOL CORE FROZEN; GATE A REMAINS OPEN PENDING ASSET-BOUND FIXTURES`

## Phase 2 conclusion

`PHASE 2 PASS — IMMUTABLE FONT AND SPACING POLICY FROZEN; GATE B SATISFIED; GATE A REMAINS OPEN PENDING PHASE 1.2 FIXTURES`

## Phase 1.2 conclusion

`PHASE 1.2 PASS — EXACT-ASSET FIXTURES FROZEN; GATE A SATISFIED`
