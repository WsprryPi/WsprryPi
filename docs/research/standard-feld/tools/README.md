# Standard Feld research tools

These standard-library-only tools generate and independently validate the Phase 1.2 fixtures. They are research tooling, not production code or application tests.

```sh
python3 docs/research/standard-feld/tools/generate_fixtures.py
python3 docs/research/standard-feld/tools/generate_fixtures.py --check
python3 docs/research/standard-feld/tools/validate_fixtures.py
```

`generate_fixtures.py --check` regenerates into a fresh temporary directory and compares every retained file byte-for-byte. `validate_fixtures.py` does not import generator transformations; it independently parses the asset, reconstructs glyphs, expands events, and checks input, timing, repeat, cancellation, and terminal-state invariants.

Neither tool accesses the network, applications, containers, audio devices, GPIO, hardware, or RF output.
