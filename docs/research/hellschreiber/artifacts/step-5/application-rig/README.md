# Contained application evidence

This compact subset records the Step 5 application gate executed by the independent private [`WsprryPi/hellschreiber-interoperability-rig`](https://github.com/WsprryPi/hellschreiber-interoperability-rig) repository at commit `dc48555df88019d0f268e6e2b2d3bbfcca8707bd`. Repository access requires authorization.

The corpus was `HELL TEST 0123456789 DE WSPRY WSPRY 73`. Gate E produced readable fldigi and xfhell self-decodes (`CONTROL PASS`). Gate F produced readable Standard Feld raster text in both cross-application directions, supporting F3. Review was manual, not blind. The fldigi startup-script warning visible in screenshots did not obscure the raster.

The two 105-labelled profiles were not cross-run: their exposed contracts cannot configure an exact reciprocal receiver (14 vs 12 positions per column, 245 vs 210 physical decisions/s, and 55 vs 210 Hz tone separation). Their disposition is `NOT CONFIGURABLE`, not a failed or passed nearest-label trial.

The retained files are the Gate D/E/F manifests and four receiver screenshots. WAVs and full logs remain excluded from this documentation subset; the private rig commit contains the scripts needed for authorized users to reproduce them. No host audio device, GPIO, radio hardware, RF output, or test network was used.

## Files

- `gate-d-manifest.json`: containment and image identity record.
- `gate-e-manifest.json`: self-decode control record.
- `gate-e-fldigi-self.png`, `gate-e-xfhell-self.png`: self-decode renders.
- `gate-f-manifest.json`: cross-application result record.
- `gate-f-xfhell-to-fldigi.png`: fldigi receiving xfhell Standard Feld.
- `gate-f-fldigi-to-xfhell.png`: xfhell receiving fldigi Standard Feld.
- `SHA256SUMS`: checksums for this compact subset.
