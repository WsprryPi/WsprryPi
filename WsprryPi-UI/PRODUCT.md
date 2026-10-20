# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

Primary users are amateur-radio operators running a dedicated transmitter
appliance based on a Raspberry Pi and companion hardware. They are technically
capable, comfortable with experimental transmission modes, and expect precise
controls rather than heavily simplified abstractions.

## Product Purpose

WsprryPi makes it possible to configure, operate, and monitor transmit-only
WSPR, QRSS, FSKCW, and DFCW workflows from a browser. Success means the operator
can understand the current state, make deliberate configuration and scheduling
decisions, and recover from failures without the interface overstating what the
software, hardware, or external services have confirmed.

## Positioning

The product combines WSPR-family transmission planning and operation with the
configuration, status, logs, spots, maintenance, update, and private-support
workflows needed to operate a dedicated Raspberry Pi transmitter appliance.
Physical transmitter backends remain explicit; hardware-free simulation is a
separate development and software-qualification mechanism and is never an
automatic fallback for missing or failed hardware.

## Operating Context

Operators typically use the interface on the same local network as the
Raspberry Pi. The principal workflows are:

- configure station, band, timing, scheduling, and transmitter-backend values;
- start, monitor, cancel, and diagnose transmission activity;
- review WSPR spot reports and application or service logs;
- inspect maintenance, update, and system state; and
- create, review, encrypt, and transfer private support bundles when assistance
  is needed.

The interface supports repeated appliance operation on desktop and mobile web
layouts, in both light and dark themes.

## Capabilities and Constraints

- Supported transmission modes include WSPR, QRSS, FSKCW, and DFCW.
- Configuration values have an end-to-end lifecycle spanning defaults, parsing,
  validation, presentation, persistence, scheduling, and runtime consumption.
- Operator-entered custom values must not be silently replaced by presets or
  normalization.
- State presented as requested, pending, persisted, active, simulated, reported,
  or confirmed must remain truthfully distinguished.
- Actions that affect transmission hardware, GPIO, services, installation,
  reboot, or shutdown require explicit operator intent and must not be inferred
  from software-only evidence.
- The hardware-free simulated backend qualifies software contracts only. It
  does not qualify RF output, physical timing, GPIO, I2C, installation, service
  behavior, or a transmitter chain.
- The editable first-party web interface lives in `WsprryPi-UI`; operator
  documentation lives in the separate `Wsprry_Pi_Docs` repository.

## Brand Commitments

The product name is Wsprry Pi, pronounced “Whispery Pi.” Product language is
precise, clean, technical, direct, and trustworthy. It should serve pragmatic
operators without hiding meaningful technical distinctions or using decorative
language in place of operational state.

## Evidence on Hand

- Product overview and licensing: `README.md`
- UI implementation and routes: `WsprryPi-UI/data`
- UI design-system record: `WsprryPi-UI/DESIGN.md`
- Hardware-free simulator contract: `docs/simulated-backend.md`
- Automated UI coverage: `WsprryPi-UI/tests`
- Debian non-hardware reference workflow:
  `.github/workflows/debian-non-hardware.yml`

No testimonials, customer counts, performance benchmarks, certification claims,
or regulatory approvals are established by these assets and future work must
not fabricate them.

## Product Principles

1. Prioritize operational clarity and truthful state over novelty.
2. Preserve explicit operator control, advanced values, and safety boundaries.
3. Keep configuration, persistence, scheduling, and runtime behavior consistent.
4. Distinguish software evidence from installation, hardware, and RF evidence.
5. Make failures understandable and provide a bounded recovery action without
   discarding the operator's work.

## Accessibility & Inclusion

The interface must support keyboard operation, visible focus, assistive-technology
state announcements, readable text, and layouts that remain usable on desktop
and mobile screens. Status and safety meaning must not depend on color alone.
