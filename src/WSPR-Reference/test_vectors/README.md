# test_vectors

This directory contains **golden reference vectors** for WSPR encoding.

## Purpose

These vectors define known-good inputs and outputs for the encoder. They are used to:

- verify correctness of the encoder
- prevent regressions during development
- validate decoder implementations
- enable round-trip testing (encode → decode)

## IMPORTANT

Do **not modify these files casually**.

If you change anything here:

- you are redefining the expected behavior of the system
- all validation based on these vectors becomes invalid

## When it is OK to update

Only update vectors if:

- you have confirmed a bug in the encoder
- you are intentionally updating to a new, verified reference implementation

## Format

Each entry includes:

- callsign
- locator
- power
- symbols (162-length string of 0–3)

## Usage

These vectors are used by the verification tool:

```bash
./verify_vectors
```

This ensures the encoder output matches the expected symbols exactly.
