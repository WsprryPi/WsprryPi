# PPM Source Weighting and Behavior Overview

This document describes how **PPMManager** supplies provider observations. The application, rather than this library, qualifies observations and selects a visible correction fallback.

## Purpose

PPM (Parts Per Million) indicates the drift between the system clock and real time. An accurate PPM value is critical for time-sensitive applications, especially on systems like the Raspberry Pi.

## Data Sources

The initial adapter reads chrony's machine-readable `tracking`, `sources`, and `sourcestats` reports. The returned snapshot includes the estimate and the quality/source metadata needed by the application.

## Source Selection Logic

The system uses the following logic to determine the PPM value:

- If chrony returns a complete report, PPMManager publishes the observation.
- If chrony is unavailable or incomplete, PPMManager publishes an unavailable reason.
- PPMManager does not substitute a short wall-clock measurement for a provider estimate.
- The consuming application decides whether to use a qualified estimate, a permitted stale estimate, fixed manual correction, or zero/uncalibrated operation.

## Qualification boundary

PPMManager reports observations; it does not declare RF calibration. Qualification thresholds, observation windows, source-change handling, stale intervals, and frame-level correction latching belong to the consuming application.

## Update Cadence and Behavior

The update loop runs every `interval_seconds` (configurable). Each loop iteration:

1. Reads the provider reports.
2. Publishes the latest provider-neutral snapshot.
3. Updates the current numeric estimate when available.
4. Triggers the registered callback so the application can reevaluate qualification, including unchanged values completing an observation window.

## Logging and Debugging

Debug output includes:

- Timestamps for each update.
- Chrony PPM.
- Provider source composition and quality state.

These are logged in a structured format that can be used for auditing or graphing the system’s timekeeping performance over time.

## Chrony as Primary Authority

Chrony is treated as the preferred source when available because it provides:

- Long-term averaging.
- Correction for hardware clock drift.
- More accurate, global time alignment.

Provider availability is not equivalent to qualification. The application evaluates the reported quality and source state.

## Summary

- The library exposes a provider-neutral observation contract.
- chrony is the initial adapter.
- Missing or unsuitable provider data is reported, not replaced silently.
- Application policy owns qualification and fallback.
