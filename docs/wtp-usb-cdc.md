# WTP USB CDC host adapter

Current normal-firmware USB management, hostname/Wi-Fi lifecycle and UTC
support are documented in [USB management](wtp-usb-management.md). Historical
phase-specific limitations below retain their original evidence scope.

Phase 10 Slice 4 adds the parent application's
[`UsbCdcStream`](../src/wtp_integration/usb_cdc.hpp). It implements the portable
WTP-Client `ByteStream` interface using POSIX serial I/O. The subsequent
[production integration](wtp-production-integration.md) links this adapter into
the application with explicit persisted endpoint selection and an owned worker.
The reusable WTP-Client library remains OS-independent.

## Endpoint selection and platform support

The caller must supply an absolute path, exact USB serial and VID/PID through
`PicoCdcSelection`. Nothing is enumerated or selected automatically. A stable
symlink such as an explicitly selected `/dev/serial/by-id` entry is permitted;
its canonical character device is inspected and opened. A changed alias on a
later open is inspected again. Retargeting an alias does not switch an already
opened descriptor to a different device.

Native identification is implemented for Linux, the Raspberry Pi deployment
target. It resolves the character device's major/minor number through sysfs,
walks bounded ancestor metadata to the USB control interface, and requires:

- CDC ACM driver `cdc_acm`, class `02`, subclass `02`, protocol `00`;
- WsprryPico WTP control interface **2**;
- the caller's exact USB serial and VID/PID.

Console control interface 0 is rejected before open. The current development
VID/PID is `cafe:4012`; it is not a production allocation. The role mapping is
defined by the [Pico USB binding][pico-usb], not by tty order or friendly names.
Its descriptor uses TinyUSB `TUD_CDC_DESCRIPTOR`, whose control-interface
protocol is `CDC_COMM_PROTOCOL_NONE` (`00`). The inspected SDK's TinyUSB source
revision was `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`; it is not a build or
runtime dependency of this adapter.

The native macOS implementation rejects opening because USB identity resolution
there has not been implemented. Its POSIX code is compiled and tested with
injected metadata and newly allocated pseudo-terminals. Adding IOKit identity
resolution or another deployment platform requires a separate implementation
and validation. Unsupported discovery never falls back to an unchecked tty.

Before configuration, the opened descriptor's character-device number and
fresh USB metadata must match the inspected identity. They are checked again
after the DTR-low interval. Missing, malformed, oversized, changed or mismatched
metadata fails closed. Setup does not send WTP probes or Console commands.
USB serial and the WTP device ID are distinct: Session must still verify HELLO's
expected device and boot identity and reconcile STATUS before mutation.

This binding relies on physical access and host device permissions, as specified
by [WTP/1][wtp]. Descriptor metadata is not cryptographic authentication. It
does not protect against a privileged actor replacing device nodes or spoofing
a USB identity. Opening a tty can itself change modem lines before userspace's
post-open checks; the checks prevent data transfer on a mismatch, not every
possible driver side effect during an adversarial device replacement.

## Opening and closing a stream

Use one event-loop owner. `CdcSystem` must outlive `UsbCdcStream`, and the stream
must outlive any attached Session. Disconnect Session before reopening or
destroying its stream. A physical open and DTR operation require the intended
device-operation authorization; calling the API is not a hardware-free action.

```cpp
wsprrypi::PosixCdcSystem system;
wsprrypi::UsbCdcStream stream(system);
// selection contains an explicitly authorized WTP endpoint and USB identity.
if (stream.begin_open(selection, monotonic_ms)) {
    // On later event-loop turns, with the same monotonic clock:
    stream.poll_open(monotonic_ms);
    // Connect the existing Session only after state() becomes Ready.
}
```

Opening uses `O_RDWR`, `O_NOCTTY`, `O_NONBLOCK`, `O_CLOEXEC` and `O_NOFOLLOW` on
the resolved path. An advisory nonblocking `flock` and tty exclusive mode are
required. Failed ownership acquisition closes only the attempted descriptor;
it does not explicitly change DTR or clear another owner's exclusivity.
These mechanisms cannot evict pre-existing noncooperating holders and are not
protection against privileged processes. See [tty exclusive-mode semantics][exclusive].

Raw 8N1 configuration disables text processing, echo, signals and software/
hardware flow control, using nominal 115200 baud, VMIN=1 and VTIME=0. The actual
terminal settings are read back and checked. No break, 1200-baud reset, terminal
logging or clock-provisioning command is used.

The adapter clears DTR and discards queued input/output with `TCIOFLUSH`.
`begin_open()` then returns in `Resetting`. The first `poll_open()` starts a
100 ms quiet interval after setup has returned; setup time cannot shorten it.
After that interval, the descriptor is reverified, queues are discarded again,
DTR is asserted, and state becomes `Ready`. There is no sleep or polling loop
inside the adapter. An absolute 2,000 ms opening deadline starts at
`begin_open`; exact expiry and a regressing clock fail closed. The quiet
interval is a host policy awaiting physical USB validation, not proof that a
particular firmware/host stack processed the DTR transition in that interval.

`close()` also cancels an opening in progress. It drops DTR, discards queues,
releases owned tty exclusivity and closes the descriptor once. It does not
drain output, restore an old terminal configuration, retry `close` after EINTR,
reopen the path or issue WTP mutations. `cleanup_failed()` reports unsuccessful
cleanup operations; consume that evidence before another `begin_open`, which
resets it. `diagnostic()` records setup/I/O failure. Retained identity after
closure is historical; only `Ready` permits transfer.

Dropping DTR aborts the byte stream, not the remote job. It cannot recall bytes
already submitted to USB or prove RF stopped. No hardware reset or remote ABORT
is implied. The existing Session retains uncertain outcomes across stream loss;
the caller must preserve that Session and use fresh HELLO/STATUS on reconnect.

## Nonblocking I/O and errors

Each read or write performs at most one zero-timeout readiness poll and one
transfer of at most 4,096 bytes. Short transfers return the actual positive
count. The adapter retains no spans, frames, suffixes or hidden byte queues;
Session owns serialization, partial-frame progress and transaction deadlines.

EINTR and EAGAIN/EWOULDBLOCK return `WouldBlock` without an internal retry loop.
Readiness hangup/error/invalid-descriptor, hard I/O errors, invalid counts and
zero-length writes of nonempty data fail and close the local stream. A zero
nonblocking tty read alone returns `WouldBlock`: POSIX permits zero for no data,
so it does not establish EOF. Readiness errors establish local disconnection;
Session deadlines handle a silent endpoint. See [termios semantics][termios].

The kernel owns syscall execution. `O_NONBLOCK`, bounded call counts and absence
of waits do not provide a hard real-time guarantee for filesystem operations,
driver ioctls or close. A later backend should keep setup off timing-critical
paths. RP2350 remains responsible for all RF event timing after ARM.

## Hardware-free validation and remaining gates

From `src`:

```sh
make wtp-usb-test wtp-protocol-test wtp-plan-test SUDO=
make wtp-usb-test SUDO= WTP_USB_BUILD_DIR=build/wtp-usb-sanitized \
  WTP_USB_CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  WTP_USB_LDFLAGS='-fsanitize=address,undefined'
make semantics-make-regression-test semantics-test-portable SUDO=
```

The focused target uses injected OS failures, synthetic sysfs metadata and
newly created PTYs only. PTY modem-control operations are replaced by a test
seam; native raw settings, readiness, binary I/O, exclusivity, queue discard,
backpressure, hangup, close and reopen are exercised. The real Session is tested
over the adapter with fragmented scripted replies, interrupted LOAD bytes,
fresh HELLO/STATUS recovery and stalled read/write deadlines. None of these
tests opens a physical serial or USB device or performs RF operation.

CI includes this target on macOS and Linux. Passing locally on macOS does not
establish a Linux runtime pass. Real Linux sysfs topology, CDC ACM driver
behavior, permissions, physical DTR reset, unplug/re-enumeration, suspend,
USB stalls and actual Pico endpoint interoperability require separately
authorized target validation. No existing campaign/RF qualification is extended.

## Documentation impact

This developer guide and the root/component progress references were updated.
No operator configuration or UI changed. The separate Wsprry_Pi_Docs backend and
INI guidance was reviewed and remains unchanged. Later integration needs
separately authorized endpoint-selection, access-permission, clock prerequisite
and unresolved-output guidance there; no udev rules or services are installed.

The [complete-plan backend](wtp-backend.md) is implemented in Slice 5 with
explicit typed construction. Slice 6 adds the [early scheduler](wtp-scheduling.md).
Production status/recovery and configuration/operator integration remain required.
Device UTC must already be independently provisioned; GET_CLOCK observes it.
Future UI work still requires a temporary UI-level development toggle and
Impeccable desktop/mobile review. Phase 10 is not complete.

[pico-usb]: https://github.com/WsprryPi/WsprryPico/blob/40812e7438f180c5e8d8ad75d4eb227271152b10/docs/development/usb-cdc.md
[wtp]: https://github.com/WsprryPi/WsprryPico/blob/40812e7438f180c5e8d8ad75d4eb227271152b10/docs/protocol/WTP.md
[exclusive]: https://man7.org/linux/man-pages/man2/TIOCEXCL.2const.html
[termios]: https://man7.org/linux/man-pages/man3/termios.3.html
