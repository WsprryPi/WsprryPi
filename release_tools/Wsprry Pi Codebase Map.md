# WsprryPi Codebase Map

## Overview

This document provides a structured map of the WsprryPi codebase to help
quickly locate functionality, understand architectural boundaries, and
debug issues efficiently.

------------------------------------------------------------------------

## Project Structure

- First-party UI component: `./WsprryPi-UI`
- Scripts and installer: `./scripts`
- Developer notes and release tooling: `./release_tools`
- Core source and reusable components: `./src/`
- Component maintenance: `./docs/components/README.md`
- Deployment target: systemd-managed service
- CLI interface available alongside daemon operation

------------------------------------------------------------------------

## System Lifecycle (End-to-End)

    systemd → daemon → config load → scheduler loop
           → plan → commit_execution_request(...)
           → backend execution → RF output

### Step-by-step

1. systemd starts the WsprryPi service
2. Application initializes configuration and subsystems
3. Scheduler loop begins
4. Scheduler:
   - Reads config / reload state
   - Determines band, timing, and message
   - Resolves PPM
   - Builds transmission plan
5. Scheduler commits execution:

       commit_execution_request(...)

6. Backend:
   - Consumes immutable request
   - Configures hardware
   - Executes transmission with precise timing
7. RF signal is produced

------------------------------------------------------------------------

## Core Architecture Layers

### 1. Input / Configuration Layer

- `src/arg_parser.cpp` --- CLI argument parsing
- `src/config_handler.cpp` --- Config normalization and persistence
- `src/INI-Handler/src/ini_file.cpp` --- INI parsing
- `src/web_server.cpp`, `src/web_socket.cpp` --- UI/API layer

------------------------------------------------------------------------

### 2. Scheduling / Policy Layer (Control Tower)

- `src/scheduling.cpp` --- Central orchestration logic
- `src/scheduling.hpp` --- Public scheduler interface
- `src/band_lookup.cpp` --- Frequency and band resolution
- `src/band_gpio*.cpp` --- Band GPIO handling

Responsibilities:

- Config reloads
- Frequency selection
- WSPR planning
- Runtime PPM handling
- Request commit boundary

------------------------------------------------------------------------

### 3. Request Contract Layer

- `src/WSPR-Transmitter/src/wspr_transmit_types.hpp`

Defines:

- `WsprTransmissionRequest`
- Transmission plan structures

This is the commit boundary contract between scheduler and backend.

------------------------------------------------------------------------

### 4. Execution / Backend Layer

- `src/WSPR-Transmitter/src/wspr_transmit.cpp` --- High-level execution
- `src/WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp` --- Hardware-specific backend
- `src/Mailbox/src/mailbox.cpp` --- Low-level Pi interaction

Responsibilities:

- Consume committed request
- Generate signals
- Handle timing and hardware

------------------------------------------------------------------------

### 5. WSPR Reference Layer

- `src/WSPR-Transmitter/src/wspr_reference_adapter.cpp` --- Integration seam
- `src/WSPR-Reference/src/wspr/wspr_ref_plan.cpp` --- Planning logic
- `src/WSPR-Reference/src/wspr/wspr_ref_encoder.cpp` --- Encoding
- `src/WSPR-Reference/src/wspr/wspr_ref_decoder.cpp` --- Decoding

Purpose:

- Clean separation of WSPR protocol logic
- Prevent leakage into scheduler/backend

------------------------------------------------------------------------

## PPM System

- `src/PPM-Manager/src/ppm_manager.hpp` --- Interface
- `src/PPM-Manager/src/ppm_manager.cpp` --- Implementation

Key concept:

- Scheduler snapshots PPM into request
- Backend must NOT re-fetch PPM

------------------------------------------------------------------------

## Source Composition

The parent repository tracks ten coherent component directories as ordinary
content:

- `WsprryPi-UI`
- `src/INI-Handler`
- `src/LCBLog`
- `src/Mailbox`
- `src/MonitorFile`
- `src/PPM-Manager`
- `src/Signal-Handler`
- `src/Singleton`
- `src/WSPR-Transmitter`
- `src/WSPR-Reference`

The source components participate in the parent build while retaining their
named roots, source hierarchies, documentation, and standalone build or test
entry points where provided. `src/LCBLog` remains independently reusable and
extractable without WsprryPi-internal dependencies. `src/WSPR-Reference`
retains its standalone CMake project, `wspr_ref_lib` API, tools, examples,
vectors, and tests. Maintenance and licensing boundaries are documented in
`docs/components/README.md`.

------------------------------------------------------------------------

## Installation & Deployment

### Installation

- Performed via scripts in `./scripts`
- Primary install method:

  curl | sudo bash (GitHub-hosted installer)

### Runtime Model

- Installed as a system service
- Managed via systemd
- Designed for unattended operation

### CLI Mode

- CLI remains available for:
  - Direct control
  - Debugging
  - Development workflows

------------------------------------------------------------------------

## UI Layer

- Located in: `./WsprryPi-UI`
- Tracked as a coherent first-party component in the parent repository
- Interfaces with backend via web server and WebSocket layer

------------------------------------------------------------------------

## Developer Tooling

- Located in: `./release_tools`
- Contains:
  - Codebase documentation
  - Release scripts
  - Internal developer notes

------------------------------------------------------------------------

## Testing Layer

- `src/tests/dial_frequency_semantics_test.cpp`

Validates:

- Dial vs RF frequency semantics
- Scheduler commit correctness
- PPM commit behavior
- Reload handling

------------------------------------------------------------------------

## Critical Invariants (Must Never Break)

### 1. Single Commit Boundary

All execution must flow through:

    commit_execution_request(...)

No bypass paths are allowed.

### 2. Immutable Execution Snapshot

Once committed:

- Request must not change
- Backend must not re-derive values
- Backend must not fetch external state (e.g., PPM)

### 3. Scheduler Owns All Policy

Scheduler decides:

- What to transmit
- When to transmit
- With what parameters

Backend is strictly execution-only.

### 4. No Hidden State Coupling

- Backend must not depend on scheduler internals
- Scheduler must not depend on backend implementation details

Only the request contract is shared.

### 5. PPM Snapshot Correctness (Critical)

- The committed request must contain the authoritative PPM value
- PPM must be resolved at commit time, not execution time
- Backend must treat PPM as immutable input
- Any drift between scheduler and backend PPM is a system bug

### 6. Test and Runtime Must Share Commit Path

- Tests must exercise the same commit path as production
- No alternate “test-only” request construction paths
- `commit_execution_request(...)` must be the single source of truth
- If tests pass but runtime fails (or vice versa), this invariant is broken

------------------------------------------------------------------------

## Recommended Reading Order

1. `src/tests/dial_frequency_semantics_test.cpp`
2. `src/scheduling.hpp`
3. `src/scheduling.cpp`
4. `src/WSPR-Transmitter/src/wspr_transmit_types.hpp`
5. `src/WSPR-Transmitter/src/wspr_transmit.cpp`
6. `src/WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp`
7. `src/WSPR-Transmitter/src/wspr_reference_adapter.cpp`
8. `src/WSPR-Reference/src/wspr/wspr_ref_plan.cpp`
9. `src/WSPR-Reference/src/wspr/wspr_ref_encoder.cpp`
10. `src/config_handler.cpp`
11. `src/band_lookup.cpp`
12. `src/PPM-Manager/src/ppm_manager.cpp`

------------------------------------------------------------------------

## Debugging Guide

### If semantics fail

→ Check the `src/scheduling.cpp` commit path

### If transmitted signal is wrong

→ Check the backend (`src/WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp`)

### If encoding is wrong

→ Check WSPR-Reference layer

### If config behaves oddly

→ Check `src/config_handler.cpp` and `src/INI-Handler`

------------------------------------------------------------------------

## Mental Model

    Config → Scheduler → Committed Request → Backend → RF Output

Scheduler is the brain. Backend is the hands.
