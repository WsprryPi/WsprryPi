# Hellschreiber Research Spike

## Status

Active — Step 1.1 documentary research complete; later research steps not yet executed.

Current authorized activity is research and documentation only.

## Purpose

This workspace is the durable record for research into the Hellschreiber family of text-transmission methods and their possible suitability for Wsprry Pi. It preserves evidence, findings, decisions, disagreements, and unresolved questions across the numbered research path.

## Scope

The spike may research historical methodology, amateur-radio variants, fonts, adoption, offline interoperability, and Wsprry Pi feasibility. Each result must remain clearly identified as evidence, inference, hypothesis, decision, or validation still required.

## Research boundaries

- Research and documentation only unless a later task explicitly authorizes implementation.
- No application, signal-generation, scheduling, configuration, CLI, web UI, or product-test changes.
- No hardware operation, GPIO keying, test tones, service changes, or over-the-air transmission.
- Operator documentation remains in the independent sibling `Wsprry_Pi_Docs` repository and is outside this spike.
- `WsprryPi-UI` and every `src/` submodule remain unmodified.

## Research path

1. Establish the reference baseline
   - 1.1 Define the reference mode
   - Additional 1.x work will be named only when its scope is approved.
2. Build the mode taxonomy
3. Freeze the font evidence
4. Measure real adoption
5. Perform offline interoperability experiments
6. Evaluate Wsprry Pi feasibility
7. Make a gated recommendation

The numbering is stable. Research reports and decisions must refer to these steps by number.

## Durable-record practice

For every materially advanced step:

1. Create or update its report under [`steps/`](steps/README.md).
2. Register material sources in [`evidence-register.md`](evidence-register.md).
3. Add or update affected decisions in [`decision-log.md`](decision-log.md).
4. Update the status and links in this index.
5. Preserve calculations, contradictions, rejected interpretations, confidence, and remaining validation—not only the preferred conclusion.

Important outcomes must be committed here rather than existing only in chat history. Temporary working notes should be omitted unless they materially explain a durable conclusion.

## Evidence standard

Claims must use the hierarchy, citation requirements, confidence labels, and conflict policy defined in the [evidence register](evidence-register.md). A repeated secondary claim is not automatically established evidence.

## Decision status

The [decision log](decision-log.md) separates accepted process decisions, provisional hypotheses, deferred decisions, rejected alternatives, and superseded conclusions. Standard Feld-Hell is now accepted as the spike's comparison baseline; it remains explicitly unselected as a Wsprry Pi implementation choice.

## Repository and branch

- Repository: `WsprryPi`
- Branch: `research/hellschreiber-review`
- Base branch: `main`
- Base commit: `4356b11265f875605f08d4f35bb012ce576b27d0`
- Setup date: 2026-07-29

## Index of step reports

| Step | Title | Status | Report | Decision impact | Last updated |
| --- | --- | --- | --- | --- | --- |
| 1.1 | Define the reference mode | Documentary research complete | [Report](steps/1.1-reference-mode.md) | HELL-DEC-0002, HELL-DEC-0006, HELL-DEC-0007 | 2026-07-29 |
| 2 | Build the mode taxonomy | Not started | Not yet created | None yet | 2026-07-29 |
| 3 | Freeze the font evidence | Not started | Not yet created | None yet | 2026-07-29 |
| 4 | Measure real adoption | Not started | Not yet created | None yet | 2026-07-29 |
| 5 | Perform offline interoperability experiments | Not started | Not yet created | None yet | 2026-07-29 |
| 6 | Evaluate Wsprry Pi feasibility | Not started | Not yet created | None yet | 2026-07-29 |
| 7 | Make a gated recommendation | Not started | Not yet created | None yet | 2026-07-29 |

## Important unresolved questions

- Which Hellschreiber variants have meaningful current amateur interoperability and activity?
- Which historical or modern font should serve as the reproducible reference?
- Which modes fit Wsprry Pi without requiring incompatible RF behavior?
- What offline and later hardware evidence would be required before implementation or transmission?

## Explicit non-goals

- Implementing a Hellschreiber transmitter
- Selecting a production mode or font before the evidence gates are satisfied
- Inventing a private Hellschreiber variant
- Modifying operator documentation
- Treating offline research as hardware, RF, regulatory, or deployment qualification
