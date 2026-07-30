# Hellschreiber Step Reports

## Convention

Each completed or materially advanced research step receives a Markdown report named with its stable step number and a concise slug, for example:

```text
steps/1.1-reference-mode.md
steps/2-mode-taxonomy.md
steps/3-font-evidence.md
steps/4-adoption.md
steps/5-interoperability.md
steps/6-wsprry-pi-feasibility.md
```

Reports may expand the template, but they must preserve the distinction among evidence, inference, provisional hypotheses, accepted decisions, rejected alternatives, unresolved questions, and validation still required.

Material sources must also be entered in [`../evidence-register.md`](../evidence-register.md), affected decisions must be updated in [`../decision-log.md`](../decision-log.md), and status must be reflected in the [spike index](../README.md).

## Report template

```markdown
# Step X — Title

## Status

## Research question

## Scope

## Method

## Sources examined

## Findings

## Source disagreements

## Calculations or measurements

## Provisional conclusions

## Decisions affected

## Unresolved questions

## Confidence assessment

## Artifacts

## Recommended next step
```

## Commit practice

At a completed step or meaningful review checkpoint:

1. Review the complete documentation diff.
2. Confirm only `docs/research/hellschreiber/` changed.
3. Run `git diff --check`.
4. Commit a concise, meaningful documentation update.
5. Push the research branch.
6. Report which durable conclusions the commit preserves.

Do not create empty status-only commits or mix unrelated repository work into a spike commit.
