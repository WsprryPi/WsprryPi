# Component maintenance

WsprryPi tracks its first-party web interface and reusable C++ components as
ordinary directories in this repository. A normal clone contains every
component needed to build and test the product.

## Component inventory

| Path | Responsibility |
| --- | --- |
| `WsprryPi-UI` | Browser interface and packaged web assets |
| `src/Band-Lookup` | Shared frequency-to-band correlation |
| `src/Chipset-Offsets` | Intrinsic chipset clock-correction values |
| `src/INI-Handler` | INI parsing and atomic configuration updates |
| `src/LCBLog` | Thread-safe application logging |
| `src/Mailbox` | Raspberry Pi mailbox and DMA-safe memory access |
| `src/MonitorFile` | File-change monitoring |
| `src/PPM-Manager` | Chrony and manual PPM selection |
| `src/Signal-Handler` | Process-signal handling |
| `src/Singleton` | Single-process ownership through a loopback socket |
| `src/WSPR-Reference` | WSPR encoding, decoding, and correlation |
| `src/WSPR-Transmitter` | Transmission planning and backend execution |

Each component root owns its source hierarchy, public interfaces, README,
standalone build or test entry points, and extraction boundary. Application
integration remains in the parent repository.

## Maintenance rules

- Treat component paths as ordinary parent-repository content.
- Review component and parent integration changes together.
- Keep reusable components independent of WsprryPi application internals.
- Preserve standalone build and test entry points where supplied.
- Do not copy generated build output into a component tree.
- Run the component's focused tests and the applicable parent integration
  tests after a change.

The repository-root `AGENTS.md` defines the complete component policy and safe
validation profiles.

## Licensing and attribution

WsprryPi-authored component code is covered by the repository-root
`LICENSE.md` unless a component or file states otherwise.

The web interface retains third-party notices and license texts under
`WsprryPi-UI/data/vendor/`. The bundled nlohmann/json header in
`src/WSPR-Reference/include/nlohmann/json.hpp` retains its embedded copyright
and SPDX notices; the corresponding license texts are stored under
`docs/licenses/nlohmann-json/`.

When extracting a component for independent use, copy its complete component
directory, preserve required sibling-component relationships, and add the
repository metadata and license files appropriate to the new distribution.
Retain all embedded and third-party notices.
