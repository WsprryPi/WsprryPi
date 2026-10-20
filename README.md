# Wsprry Pi

Wsprry Pi (pronounced “Whispery Pi”) transmits WSPR, QRSS, FSKCW and DFCW using
a Raspberry Pi. It supports local schedules, test tones, connected Pico
transmitters, and fleets of independent transmitters controlled from its web
interface.

[![Documentation Status](https://readthedocs.org/projects/wsprry-pi/badge/?version=stable)](https://wsprry-pi.readthedocs.io/en/stable/?badge=stable)

## Transmitters and operation

Local output uses the supported Raspberry Pi GPIO backend or a Si5351 clock
generator. Pi 5-family GPIO output uses the separately maintained
RP1-GPCLK-DKMS provider. Available modes and frequencies depend on the selected
backend and its admission policy. Software support does not establish RF power,
frequency accuracy, spectral performance or suitability of a physical setup.
Use suitable filtering, attenuation and a load for the selected output path.

WsprryPico connects over USB or a supported network connection. Complete jobs
execute on the transmitter, so symbol timing does not depend on continuous
host delivery. Supporting Pico firmware also provides settings management,
independent station-field saves, firmware installation and retained recovery.
Firmware erase is a separate explicit action. Saving settings and applying
saved station progress remain separate from transmit readiness.

A Pi can also serve as a network transmitter while running its own schedule.
The controller supports up to eight independent remote output schedules.
Select Fleet in Setup to manage remote transmitters. Ownership, output
capabilities and recovery state govern whether a job can start.

## Installation and use

The [operator manual](https://wsprry-pi.readthedocs.io/en/stable/) covers
background, installation, configuration and operation.

The installation entry point is:

```sh
curl -L installwspr.aa0nt.net | sudo bash
```

RP1 installation establishes neutral runtime administration. Selecting GPIO4
or GPIO20 is a later explicit action; installation and route selection do not
enable output. Uninstallation preserves a pre-existing, changed or ambiguously
owned RP1 provider. `REMOVE_RP1_GPCLK_DKMS=false` also retains a proven
WsprryPi-owned provider.

## Development and testing

The [development guide](docs/development/README.md) covers builds, components,
contracts and hardware-free validation. The explicit
[simulated backend](docs/simulated-backend.md) exercises planning, scheduling,
cancellation, status and failure handling on Debian without hardware access.
Simulation does not qualify timing, GPIO, installation, services or RF output.

[Container builds](docs/container-builds.md) target Raspberry Pi OS Bookworm
and Trixie on ARMv6 and AArch64, including 32-bit Pi 1/A+ and Pi Zero.
[Precompiled installation](docs/precompiled-installation.md) verifies artifact
identity and compatibility before deployment.

Selected work and release gates are maintained in the
[release punchlist](release_tools/Release%20Punchlist.md).

## License and provenance

Versions 2.x and later use the [MIT License](LICENSE.md). Earlier WsprryPi
versions used GPLv3. The original PiFM source is retained in
[historical/pifm.tar.gz](historical/pifm.tar.gz) for source provenance.
Component licenses and attribution remain with their respective sources.
