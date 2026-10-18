# Selected Pico USB firmware access

The source implementation adds **Pico firmware over USB** beside selected Pico
controls in Setup. It is under validation and is not yet a released package.
The shared core remains owned by WsprryPico; WsprryPi provides protected routes,
process/permission boundaries, selected-member maintenance and presentation.

The default firmware branch is **main**, independently of WsprryPi's branch.
The operator can explicitly choose devel, a specific CI version or stable.
Download and check image resolves the canonical public image without touching
the device. An unavailable main image or compatible stable release is refused
without substitution. Installation needs a successful plan for the current USB
serial, device and selection, followed by explicit confirmation. Changing the
selector invalidates the displayed plan. Developer builds remain a host CLI
workflow; browser clients cannot request compilation.

The backend invokes `/usr/bin/python3 -I` with the fixed deployed entry path
`/usr/local/lib/wsprrypi/pico-usb/entry.py`, direct argument arrays, bounded output
and a deadline. It validates `wsprrypico-usb-result/1`, installer version and exit
status. The adapter checks root ownership/protection and the pinned core/schema
hashes, then executes the verified source without a stale bytecode cache.
Release selection, identity, UF2 bounds, backup, programming and preservation
are implemented only in the shared core. The browser cannot pass a command,
filesystem path, download URL or arbitrary device.

`GET/POST /api/v1/member/firmware` uses existing privileged API authorization;
POST additionally requires the same-origin mutation context. Closed request
shapes bind the current `selection_id`. Plan defaults to main when branch is
omitted. Upgrade requires its current `plan_id` and `confirmed: true`. Recover
uses only the selected serial's private retained record. No USB update is
offered for a network-only Pico.

Before upgrading, the runtime obtains fresh idle/unowned admission, detaches
only the selected member and durably creates
`/var/lib/wsprrypi/pico-usb-control/SERIAL.json`. Runtime and installer share
per-serial locks under `/run/lock/wsprrypico`. Firmware preflash/postboot holds
protect the standalone scheduler as described in WsprryPico's
`docs/development/usb-installer.md` in the integrated Pico `devel` source.
A failed/ambiguous update keeps that member blocked across service restart.
After core verification, a serialized reconnection checks the fresh device
identity and version while the durable marker still blocks scheduling. The
marker is removed only after reconnection succeeds. Other fleet members keep
their existing admission rules.

Recover this update can finish a retained core transaction or retry Pi
reconnection after verified programming. Host loss before the Pi receives the
private transaction path may require administrator recovery: inspect
`/var/lib/wsprrypi/pico-usb` over the host console, bind the correct serial and
use the core's explicit `recover --transaction ... --device ... --board pico2_w
--yes` command. A record without a completed backup is not permission to program.
Do not delete a marker to bypass an unidentified state or restore another
board's backup. Deliberate BOOTSEL reconnect may be necessary. Browser status
omits private paths and never contains flash/credential bytes.

## Package lifecycle

`config/pico-usb-installer.json` freezes the compatible WsprryPico source
revision, installer version and core/schema SHA-256. `scripts/pico_usb_deploy.py`
is called only by the established package lifecycle, not by a browser click.
It fetches those exact anonymous HTTPS files and validates all bytes before
publishing executable content. An update refuses changed administrator-owned
installer bytes and outstanding Pico maintenance. Removal deletes only
unchanged owned files and retains private backups, markers and caches.

The package creates private root-owned backup/control/cache directories,
`dialout` USB permissions for WsprryPico CDC and RP2350 BOOTSEL, and a setgid
per-device lock directory restored by tmpfiles at boot. Permissions apply to
new USB enumeration after rules reload; reconnect a cable when necessary.
Python 3.10+ and a USB-enabled pinned picotool 2.3.0 are required. It does not
install an SDK, compiler, picotool or change unrelated GPIO/backend settings.

The deployment is pinned to integrated Pico revision
`cc12ca45eb66a95e518b81119e3d6ca546db1547`, with exact core/schema hashes in the
descriptor. Its installer version is `0.1.0`; firmware selection still defaults
to main, with explicit devel selection. A reviewed live package deployment and
physical Pi-to-Pico USB installation/preservation remain open. Publishing source
and validating package staging do not establish those physical results.

## Validation and documentation impact

Hardware-free tests cover the process result/argv contract, durable private
holds, task exclusion, route authorization classification, immutable package
payload integrity/removal and UI states. The actual source-owned PHP/JS UI was
rendered at desktop/mobile sizes in light/dark for checked, pending, refusal,
failure, reconnect and success, without horizontal overflow. Impeccable's
detector returned no findings. This is presentation/software evidence only.
The committed Pico revision and both descriptor hashes were also checked against
Git object bytes, then staged and removed through the real package adapter in a
temporary root. The Linux C++ sources match the retained validation snapshot;
installer, browser API and USB-management binaries pass on wspr4.

The user authorized wspr4 for a temporary Linux build. Source and extracted
OpenSSL development headers stay under `/tmp/wsprrypi-usb-installer-20261018`;
its installed packages, checkout, services and RF hardware were not changed.
The Mac's C++ toolchain was not used for WsprryPi compilation. On Debian 13.7
aarch64 with g++ 14.2, the installer C++ tests and configuration/runtime client
compile pass. Browser API and USB-management test binaries pass in that
same hardware-free tree. Build metadata is disabled for this exported non-Git
snapshot. JavaScript tests run on the Mac because wspr4 has no Node runtime;
this is not a Linux aggregate/whole-suite pass. The changed web control-route
translation unit passes a separate Linux syntax check. Independent read-only
review reports no remaining actionable source/doc findings and independently
reran the five deployment/adapter tests, UI unit, Impeccable detector, Bash
syntax and diff checks. It did not rerun Linux C++ compilation.

The separate operator documentation repository needs instructions for branch
selection, dependency/USB permissions, recovery and the selected Pico firmware
controls after acceptance. No separate docs repository was edited or published.
