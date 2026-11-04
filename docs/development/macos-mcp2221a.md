# Native macOS MCP2221A transport design

The selected API is Apple's public IOKit IOHIDDevice API, linked with the system
IOKit and CoreFoundation frameworks. No HIDAPI binary, private Apple API,
vendor SDK, identity rewrite, or bundled USB driver is required. Apple maintains
and ships the API as part of macOS. WsprryPi code retains its repository license;
system frameworks are supplied by macOS, not redistributed with the executable.
The initial build target is macOS 14 or later, arm64 and x86_64, with Apple Clang
and the existing OpenSSL dependency. This is a source-build target; executables
inherit their SDK/OpenSSL deployment requirements. No universal, signed, or
notarized binary distribution is provided. Runtime/electrical qualification is separate
from a successful build on either architecture.

HIDAPI 0.15.0 was evaluated but not selected: its macOS `hid_write` uses synchronous
`IOHIDDeviceSetReport` without an explicit submission deadline. The native design
uses `IOHIDDeviceSetReportWithCallback` and a dedicated per-exchange run-loop mode
with bounded polling. Input reports must be exactly 64 bytes and echo the request.
Report buffers and callback state remain session-owned through close and
IOHIDDevice release. Exchanges are scheduled on a private run-loop mode; no
background loop retains callback access after unscheduling and release.
[Apple’s HID implementation](https://github.com/apple-oss-distributions/IOHIDFamily/blob/main/IOHIDLib/IOHIDDeviceClass.m)
tracks pending report contexts until completion or device deallocation.

Discovery reads IORegistry properties without opening devices or submitting HID
reports. Identity is `usb:04d8:00dd:serial:<hex UTF-8 bytes>` when a serial exists,
otherwise `usb:04d8:00dd:path:<hex UTF-8 location token>`, where the token is
`macos-location-` plus eight lowercase hexadecimal digits of the USB location ID.
Topology identities bind this host/controller/physical port and do not promise
portability across ports, docks, hosts, or controller changes. Duplicate identities
are refused, including duplicate serials. A live session also retains the registry
entry ID, so unplug/replug invalidates it even at the same location. A newly
exposed serial invalidates a former topology identity. No arbitrary first match,
Linux bus emulation, or reconnect is provided.

The protocol engine supports the Si5351 contract only: 1–9 byte writes, one-byte
reads, and pointer-write with STOP followed by read. Parameter blocks never exceed
nine bytes including their register pointer. Repeated-start and larger transfers
are rejected rather than silently mapped. Speed is explicitly set to 100 kHz
(divider 117). A successful HID submission is only command admission; completion
requires checked engine state and transfer counts. Responses, NACK, timeout,
busy, partial, malformed, disconnected, and uncertain sessions are distinct.
The complete pointer/read operation is serialized with device register access.
Each complete I2C operation has a 500 ms software deadline and at most 100
polls per data/completion wait phase;
transfer cancellation uses a fresh bounded deadline and at most 50 polls.
Apple documents asynchronous report-submission timeouts in milliseconds.
OS scheduling, registry discovery, open, and close are not hard-real-time output
inhibition guarantees.
No configuration write is replayed on a failure.

Idle cancellation is prohibited: the November 2 reference experiment observed
STOP-timeout 0x62 after idle cancellation. A non-idle initial engine is refused.
Cancellation of an owned active transfer is bounded and its resulting engine
state must be verified. A failed session never automatically resumes. Adapter
cancellation does not disable a separately powered Si5351. Existing backend
startup inhibition and output-disable cleanup remain authoritative; disconnect
or a faulted session can prevent confirmation and must be reported as uncertain.

Build explicitly with `make -C src BACKENDS=si5351 ANCILLARY_GPIO=0
SI5351_TRANSPORT=mcp2221a-hid release SUDO=` on macOS. Linux retains `linux-i2c`
and the kernel-owned `hid-mcp2221` path. No direct HID transport is built on Linux.
No automatic transport, bus, simulation, or physical-backend fallback is allowed.
The selector rules live in the reusable transmitter component. Both builds
consume that one policy; standalone objects and dependency files are isolated
by transport, and the parent retains its transport-specific profile binaries.

Preferred hardware (unqualified by software tests):
[Adafruit MCP2221A Breakout — product 4471](https://www.adafruit.com/product/4471)
and [Adafruit Si5351A Clock Generator with STEMMA QT — product 5640](https://www.adafruit.com/product/5640),
25 MHz onboard crystal, 7-bit address `0x60`.

Primary references:

- [Microchip MCP2221A datasheet DS20005565D](https://ww1.microchip.com/downloads/en/DeviceDoc/MCP2221A-Data-Sheet-DS20005565D.pdf), sections 3.1.1, 3.1.5, 3.1.6, and 3.1.10.
- [Apple IOHIDDevice API](https://developer.apple.com/documentation/iokit/iohiddevice_h).
- [Apple asynchronous report submission](https://developer.apple.com/documentation/iokit/1438422-iohiddevicesetreportwithcallback).
- [HIDAPI 0.15.0 macOS implementation](https://github.com/libusb/hidapi/blob/hidapi-0.15.0/mac/hid.c).

Physical qualification, final #411 acceptance, published operator documentation,
final merge, and the deferred machine-power UI slice remain #419 acceptance gates.

## Configuration and remaining operator documentation

`--si5351-transport` is a read-only compiled-transport report, not an availability
probe. `--list-i2c-adapters` lists identities from native metadata without
opening devices. A serial or topology identity must resolve to exactly one
64-byte-report MCP2221A endpoint. Configure that exact string using the existing
`Si5351.Adapter Identity` INI setting or `--si5351-adapter-identity` CLI option.
The retained Linux I2C bus setting has no device-selection role on macOS.
Only the explicitly configured Si5351 address is probed for live validation.
Native CLI access is implemented; the web adapter selector and cached native
status workflow are not yet supported and must not be treated as available.

The separate `WsprryPi-Docs` repository was inspected read-only. Before operator
publication, add a native macOS installation/setup guide alongside
`docs/Install/linux_si5351.md`, link it from `docs/Install/index.md`, and document
build selection, OpenSSL, metadata identity, access denial, topology limits,
CLI configuration, qualified hardware, and explicit recovery/output uncertainty.
Update UI documentation only when the separately implemented native UI workflow
has passed Impeccable review. No operator-documentation publication is claimed.
