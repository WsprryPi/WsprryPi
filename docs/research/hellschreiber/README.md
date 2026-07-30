# Hellschreiber Research Spike

## Status

Complete — Steps 1.1 through 7 are complete for their scoped research questions. Step 7 recommends Standard Feld-Hell as the sole subject of a separately authorized implementation-design and qualification-planning spike, subject to seven mandatory gates. This closes the research question; it does not authorize implementation or establish hardware, RF, regulatory, release, or deployment readiness. fldigi `FSKH105` and xfhell `FMHell105` remain separate deferred contracts, a blended generic “Hell 105” contract is rejected, and the documentary six-row/105-baud profile remains insufficient evidence.

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
   - 1.2 Define the comparison and evidence framework
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

The [decision log](decision-log.md) separates accepted process decisions, provisional hypotheses, deferred decisions, rejected alternatives, and superseded conclusions. Standard Feld-Hell is the comparison baseline and the sole candidate recommended for a later design-and-qualification-planning spike; it remains explicitly unselected and unimplemented in Wsprry Pi.

## Repository and branch

- Repository: `WsprryPi`
- Branch: `research/hellschreiber-review`
- Base branch: `main`
- Base commit: `4356b11265f875605f08d4f35bb012ce576b27d0`
- Setup date: 2026-07-29

## Index of step reports

| Step | Title | Status | Report | Decision impact | Last updated |
| --- | --- | --- | --- | --- | --- |
| 1.1 | Define the reference mode | Documentary research complete; raster model refined by Step 3 | [Report](steps/1.1-reference-mode.md) | HELL-DEC-0002, HELL-DEC-0006 (superseded), HELL-DEC-0007, HELL-DEC-0019 | 2026-07-29 |
| 1.2 | Define the comparison and evidence framework | Documentary and methodology research complete | [Report](steps/1.2-comparison-framework.md) | HELL-DEC-0008 through HELL-DEC-0012 | 2026-07-29 |
| 2 | Build the mode taxonomy | Documentary taxonomy complete | [Report](steps/2-mode-taxonomy.md) | HELL-DEC-0013 through HELL-DEC-0018 | 2026-07-29 |
| 3 | Freeze the font evidence | Documentary evidence complete; research references frozen | [Report](steps/3-font-evidence.md) | HELL-DEC-0019 through HELL-DEC-0022 | 2026-07-29 |
| 4 | Measure real adoption | Public documentary research complete; no representative worldwide mode-share ranking available | [Report](steps/4-adoption.md) | HELL-DEC-0023, HELL-DEC-0024 | 2026-07-29 |
| 5 | Perform offline interoperability experiments | Scoped matrix complete: Standard Feld F3 bidirectional; 105-labelled profiles `NOT CONFIGURABLE` as exact reciprocal contracts | [Report](steps/5-interoperability.md) | HELL-DEC-0025 through HELL-DEC-0029 | 2026-07-30 |
| 6 | Evaluate Wsprry Pi feasibility | Source-level feasibility complete; Standard Feld advances with conditions | [Report](steps/6-wsprry-pi-feasibility.md) | HELL-DEC-0030 through HELL-DEC-0035 | 2026-07-30 |
| 7 | Make a gated recommendation | Complete; Standard Feld recommended only for a separately authorized design-and-qualification-planning spike | [Report](steps/7-gated-recommendation.md) | HELL-DEC-0036 through HELL-DEC-0042 | 2026-07-30 |

## Questions transferred to the proposed next spike

- Which immutable, redistributable Standard Feld font and spacing policy should Wsprry Pi propose?
- Does that exact asset retain F3, or achieve F4, with named receiver versions under repeatable scoring?
- Can an existing backend meet objective timing, jitter, cancellation, safe-idle, and spectral criteria on the proposed supported Raspberry Pi matrix, or is a new backend required?
- What named occupied-bandwidth or emission criterion and worst-case raster pattern should govern later RF qualification?
- What product, operator, validation, and release contracts must a future implementation satisfy?

Longer-term research remains open for representative mode-share observations, deliberately substituted 105-profile trials, licensed 105/Hell-80 assets and endpoints, and independent verification or redistribution permission for the historical drum transcription.

## Explicit non-goals

- Implementing a Hellschreiber transmitter
- Selecting a production mode or font before the evidence gates are satisfied
- Inventing a private Hellschreiber variant
- Modifying operator documentation
- Treating offline research as hardware, RF, regulatory, or deployment qualification
