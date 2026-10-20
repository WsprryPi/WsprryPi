# Chipset offsets

Header-only, hardware-independent selector shared by GPIO RF backends and
clock models. This component owns the intrinsic offset table; it has no
application, provider, or transmitter dependency. Include `chipset_offsets.hpp`
with this component's `src` directory on the include path.

| Clock chipset | Intrinsic offset (ppm) |
| --- | ---: |
| BCM2835 (Pi1) | -2.5 |
| BCM2836 / BCM2837 | 0 |
| BCM2711 | 0 |
| RP1 | -46.245 |

Unknown chipsets throw rather than silently acquiring a zero default. These are
product policy defaults, not individual-board calibration certificates.

Consumers add the selected offset exactly once to their caller's manual or
system-clock correction when planning RF dividers:
`nominal_parent_hz * (1 + (caller_ppm + intrinsic_ppm) / 1000000)`.
Do not apply it to DMA timing, the nominal parent identity, SDR calibration,
or persistent user settings. Consumers retain independent validation of the
caller's PPM range. Selecting an offset never operates hardware.

Run `make test` here, or `make chipset-offsets-test` from the parent `src`.
Tests require only a C++17 compiler and operate no hardware. For extraction,
keep this directory together and include the repository's MIT `LICENSE.md`.
