# Step 5 offline interoperability artifacts

The [`application-rig/`](application-rig/README.md) directory contains the compact Gate D/E/F application evidence subset tied to commit `dc48555df88019d0f268e6e2b2d3bbfcca8707bd` in the private [`WsprryPi/hellschreiber-interoperability-rig`](https://github.com/WsprryPi/hellschreiber-interoperability-rig) repository. It supplements the source-derived fixtures below; it does not replace or modify them. Repository access requires authorization.

This directory contains the minimal reproducible fixtures used by Step 5. Nothing here opens an audio device, accesses GPIO, keys a transmitter, or produces RF.

## Reproduction

Obtain these upstream source archives and verify their SHA-256 values:

| Source | Upstream archive | SHA-256 |
| --- | --- | --- |
| fldigi 4.2.12 | `fldigi-4.2.12.tar.gz` from SourceForge | `028bcb1c100cb790cad36324b8063c13594e160743f9378320ceabcf16dbc44a` |
| xfhell 3.5.2 | `xfhell-3.5.2.tar.bz2` from QSL.net | `7b16ecdaa425ebc19f7e555fe28473e0228ea344e0203062a4e408346e75b209` |
| RadioLib | commit `0795caa41c6350a2f862137cfc22528c2aaad2bc` archive from GitHub | `6bf3c67958c8fe97d019560e11fb2b99231ff4b86c61aa7e461db640bf396150` |

After extracting them, run:

```sh
python3 scripts/analyze_interop.py \
  --fldigi /path/to/fldigi-4.2.12 \
  --xfhell /path/to/xfhell-3.5.2 \
  --radiolib /path/to/RadioLib-0795caa41c6350a2f862137cfc22528c2aaad2bc \
  --historical /temporary/path/to/hell-keyboard-encoding.txt \
  --output .
```

The historical transcription must be retrieved from the URL frozen in Step 3 and must hash to `1dc94ee5ba9aa62acecfe0607a1109c48e286595c34931095fc3b3a8bdae93d5`. It is read temporarily and is not copied into this directory. The utility uses only the Python standard library. Running it refreshes all derived files and `manifest.json`.

## Contents

- `corpus/corpus.txt` — deterministic character and short-exchange corpus.
- `scripts/analyze_interop.py` — source parser, raster comparator, offline CPFSK fixture generator, and bounded tone-decision impairment check.
- `measurements/summary.json` — source identities, normalized contracts, aggregate raster results, and audio metadata.
- `measurements/raster-comparison.csv` — character-level transmitted-cell and trimmed-glyph comparisons.
- `measurements/impairment-results.csv` — deterministic alternating-symbol tone decisions under offsets, inversion, and noise.
- `rasters/*.pbm` — auditable monochrome renderings of the focus-character set.
- `audio/*.wav` — PCM fixtures for the two conflicting 105-labelled source contracts.
- `manifest.json` — path, size, and SHA-256 for every retained input or derived artifact except itself.

## Interpretation boundary

The WAV files and tone decisions are source-derived fixtures, not recordings captured from the applications. The impairment check evaluates only binary tone decisions. It does not model either application's complete filters, AGC, deskew, waterfall rendering, operator tuning, or human recognition. A clean cross-contract tone decision result therefore cannot be promoted to application interoperability.

## Licensing

The analysis utility and corpus are released under the repository's MIT license. Fixtures derived from fldigi or xfhell source and fonts are provided under GPL-3.0-or-later, matching those projects. RadioLib-derived rasters retain RadioLib's MIT terms. No historical drum transcription is copied here.
