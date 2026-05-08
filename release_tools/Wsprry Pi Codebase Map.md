# WsprryPi Codebase Map

## Overview

This document provides a structured map of the WsprryPi codebase to help
quickly locate functionality, understand architectural boundaries, and
debug issues efficiently.

------------------------------------------------------------------------

## Project Structure Additions

- UI lives in submodule: `./WsprryPi-UI`
- Scripts and installer: `./scripts`
- Developer notes and release tooling: `./release_tools`
- Core source directory: `./src/` (contains multiple submodules)
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
- `INI-Handler/src/ini_file.cpp` --- INI parsing
- `src/web_server.cpp`, `src/web_socket.cpp` --- UI/API layer

------------------------------------------------------------------------

### 2. Scheduling / Policy Layer (Control Tower)

- `src/scheduling.cpp` --- Central orchestration logic
- `src/scheduling.hpp` --- Public scheduler interface
- `src/wspr_band_lookup.cpp` --- Frequency and band resolution
- `src/band_gpio*.cpp` --- Band GPIO handling

Responsibilities:

- Config reloads
- Frequency selection
- WSPR planning
- Runtime PPM handling
- Request commit boundary

------------------------------------------------------------------------

### 3. Request Contract Layer

- `WSPR-Transmitter/src/wspr_transmit_types.hpp`

Defines:

- `WsprTransmissionRequest`
- Transmission plan structures

This is the commit boundary contract between scheduler and backend.

------------------------------------------------------------------------

### 4. Execution / Backend Layer

- `WSPR-Transmitter/src/wspr_transmit.cpp` --- High-level execution
- `WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp` --- Hardware-specific backend
- `Mailbox/src/mailbox.cpp` --- Low-level Pi interaction

Responsibilities:

- Consume committed request
- Generate signals
- Handle timing and hardware

------------------------------------------------------------------------

### 5. WSPR Reference Layer

- `WSPR-Transmitter/src/wspr_reference_adapter.cpp` --- Integration seam
- `WSPR-Reference/src/wspr/wspr_ref_plan.cpp` --- Planning logic
- `WSPR-Reference/src/wspr/wspr_ref_encoder.cpp` --- Encoding
- `WSPR-Reference/src/wspr/wspr_ref_decoder.cpp` --- Decoding

Purpose:

- Clean separation of WSPR protocol logic
- Prevent leakage into scheduler/backend

------------------------------------------------------------------------

## PPM System

- `PPM-Manager/src/ppm_manager.hpp` --- Interface
- `PPM-Manager/src/ppm_manager.cpp` --- Implementation

Key concept:

- Scheduler snapshots PPM into request
- Backend must NOT re-fetch PPM

------------------------------------------------------------------------

## Source Composition

The `./src/` directory includes multiple submodules that are compiled
into the final binary. These represent independently maintained
components that form the runtime system.

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
- Maintained as a separate submodule
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

1. `dial_frequency_semantics_test.cpp`
2. `scheduling.hpp`
3. `scheduling.cpp`
4. `wspr_transmit_types.hpp`
5. `wspr_transmit.cpp`
6. `wspr_transmit_backend_rpi.cpp`
7. `wspr_reference_adapter.cpp`
8. `wspr_ref_plan.cpp`
9. `wspr_ref_encoder.cpp`
10. `config_handler.cpp`
11. `wspr_band_lookup.cpp`
12. `ppm_manager.cpp`

------------------------------------------------------------------------

## Debugging Guide

### If semantics fail

→ Check `scheduling.cpp` commit path

### If transmitted signal is wrong

→ Check backend (`wspr_transmit_backend_rpi.cpp`)

### If encoding is wrong

→ Check WSPR-Reference layer

### If config behaves oddly

→ Check `config_handler.cpp` and INI handling

------------------------------------------------------------------------

## Mental Model

    Config → Scheduler → Committed Request → Backend → RF Output

Scheduler is the brain. Backend is the hands.
