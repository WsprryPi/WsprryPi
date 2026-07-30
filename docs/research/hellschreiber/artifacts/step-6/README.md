# Step 6 feasibility calculations

These compact calculations support the Step 6 source-level feasibility report. They do not execute Wsprry Pi, open an audio device, access GPIO, configure a transmitter, or produce RF.

## Basis

- Source snapshot: Wsprry Pi commit `7514ac95b01fbff17065781f69c3c04028bed66f`.
- Comparison corpus: `HELL TEST 0123456789 DE WSPRY WSPRY 73` (38 characters including spaces).
- Fixed comparison cell: seven columns per character at 17.5 columns/s.
- Duration: `38 × 7 / 17.5 = 15.2 s` for every candidate under that common cell assumption.
- Physical-position count: `characters × columns × physical positions per column`.
- Conservative event-memory bound: one event per physical position at 128 bytes/event. This is a planning bound, not a measured `sizeof(RfEvent)` and not a runtime-memory measurement.

The common seven-column cell isolates physical decision rate and row geometry. Real implementation duration can differ when a selected font uses variable widths, different leading/trailing spacing, or additional leader/trailer material.

## Files

- `candidate-calculations.csv`: transparent timing, event-count, and conservative memory arithmetic.
- `SHA256SUMS`: checksums for the retained Step 6 artifacts other than itself.

## Reproduction

For each row:

```text
position_duration_ms = 1000 / physical_decision_rate_per_s
corpus_duration_s = corpus_characters × columns_per_character / columns_per_s
physical_positions = corpus_characters × columns_per_character × physical_positions_per_column
event_memory_upper_bound_bytes = physical_positions × 128
```

The documentary six-row profile is listed at its logical 105 decisions/s. An implementation that duplicates each logical row into a physical pair would instead use 12 physical positions/column at 210 positions/s while retaining the same 15.2-second cell duration.
