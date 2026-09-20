<!-- omit in toc -->
# Wsprry Pi

*A QRP WSPR transmitter leveraging a Raspberry Pi*, (pronounced: (Whispery Pi))

[![Documentation Status](https://readthedocs.org/projects/wsprry-pi/badge/?version=stable)](https://wsprry-pi.readthedocs.io/en/stable/?badge=stable)

<!-- omit in toc -->
## Table of Contents

- [License](#license)
- [Origins](#origins)
- [Development and testing](#development-and-testing)
- [Installation](#installation)

## License

Versions 2.x+ of this project are distributed the [MIT License](LICENSE.MIT.md).

## Origins

This idea likely traces its origins to an idea Oliver Mattos and Oskar Weigl presented at the PiFM project. While the website is no longer online, the Wayback Machine has [the last known good version]( http://web.archive.org/web/20131016184311/http://www.icrobotics.co.uk/wiki/index.php/Turning_the_Raspberry_Pi_Into_an_FM_Transmitter).

The original PiFM code is still hosted by the icrobotics.co.uk website. However, I suspect the domain has fallen into disrepair and may be unsafe, and no direct links are provided here. You can use the link above to see the site; should the code disappear, I have [saved it here](./historical/pifm.tar.gz).

After a conversation with Bruce Raymond of TAPR; I forked @threeme3's repo and provided some rudimentary installation capabilities and associated orchestration.  Version's 1.x of this project were a fork of threeme3/WsprryPi, licensed under the GNU General Public License v3 (GPLv3). The original project is no longer maintained.

In late 2024, George [K9TRV] of TAPR reached out to me about some questions related to using WsprryPi on the Pi 5.  While I have not yet made that jump, the conversation spurred me to discard the original code in favor of a more modern, extensible, and maintainable base.

Version 2.x+ is re-written from scratch, is no longer a derivative work, and is released under the MIT license.

## Development and testing

Developers can exercise WsprryPi's application planning, scheduling,
cancellation, status, failure, and cleanup paths on an ordinary Debian machine
without GPIO, MMIO, mailbox, DMA, I2C, transmitter device nodes, or RF hardware.
See the [hardware-free simulated backend guide](docs/simulated-backend.md) for
building, explicit selection, deterministic virtual-time traces, bounded
real-time tests, fault injection, repeated execution, Debian CI, and the limits
of simulation evidence.

The [WTP-Client component](src/WTP-Client/README.md) provides the portable WTP/1
wire and session foundation for Phase 10 Pico integration. The parent
[execution-plan converter](docs/wtp-execution-plan.md) prepares complete finite
WTP jobs, and the [USB CDC adapter](docs/wtp-usb-cdc.md) provides the Linux host
transport. The [complete-plan backend](docs/wtp-backend.md) now composes these
layers through the shared transmitter controller, with hardware-free tests.
The [early scheduler](docs/wtp-scheduling.md) freezes complete requests and hands
off their exact UTC starts before the RF boundary. Its [status and recovery
boundary](docs/wtp-status-recovery.md) publishes coherent observations and retains
explicit recovery results. [Production integration](docs/wtp-production-integration.md)
adds persisted endpoint selection, an owned runtime worker and explicit status/
recovery controls behind a default-off UI development toggle. Physical USB,
target operation and RF qualification are accepted only within the bounded
Phase 10/11 records; broad mode/band/clock, timing, spectral, reliability and
release qualification remain outstanding in Phase 13.

For containerized ARMv6 and AArch64 builds targeting Raspberry Pi OS Bookworm
and Trixie, including 32-bit Pi 1/A+ and Pi Zero support, see the
[container build guide](docs/container-builds.md). For installing those artifacts,
see the [precompiled installer contract](docs/precompiled-installation.md).

RP1 GPCLK support is compiled into the application as a consumer of the
independently maintained `/dev/rp1-gpclk` provider. On Pi 5-family systems the
installer can resolve and validate an eligible published provider release; it
also supports fail-closed exact-source development selection. See the
[RP1-GPCLK-DKMS installation contract](docs/rp1-gpclk-dkms-installation.md).
On uninstall, WsprryPi removes only an unchanged provider that its secure
ownership record proves WsprryPi installed; all pre-existing, legacy-recorded,
ambiguous, or drifted providers are preserved. Operators can set
`REMOVE_RP1_GPCLK_DKMS=false` to retain even a proven owned provider. WsprryPi
establishes only route-neutral runtime administration during installation. It
waits for a later explicit GPIO4/GPIO20 operator choice before executing a
digest-reviewed route plan, and it never enables output as part of installation
or route selection. Runtime checks establish application compatibility only; they do
not qualify installation, GPIO behavior, timing, frequency accuracy, spectral
performance, or RF output.

## Installation

Please see [the documentation](https://wsprry-pi.readthedocs.io/en/stable/) for background, installation, and use.

If you want to get started and not read anything, here is how to install it:

``` bash
curl -L installwspr.aa0nt.net | sudo bash
```

Authenticated Pico network integration is documented in
[WTP network configuration](docs/wtp-network.md) and the
[shared browser API](docs/wtp-browser-api.md). See the
[Phase 11.1 software acceptance record](docs/development/phase11-1-review.md)
for the original host-software review and its Phase 11.7 addendum. WsprryPico's
[authoritative joint closure](https://github.com/WsprryPi/WsprryPico/blob/devel/docs/development/phase11-7-review.md)
closes Phase 11 within its documented software, bounded physical and scoped
conducted-RF acceptance; it does not claim release qualification.
