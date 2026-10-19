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
and a deadline. It validates `wsprrypico-usb-result/1`, installer contract and exit
status. The adapter checks root ownership/protection and the retained core/schema
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

For verified older CI firmware, **Install checked image** prepares a one-time
BOOTSEL migration instead of attempting an unsupported automatic reset. The
Pico must be idle, unowned, and have its saved standalone schedule disabled or
unconfigured. No firmware is written during preparation. The page instructs
the operator to unplug the selected Pico, hold BOOTSEL while reconnecting to
the same Pi USB port, release BOOTSEL, then choose **Continue after BOOTSEL**.
Continuation uses the private retained transaction, verifies the same board
and old application, completes a verified full backup, and checks reserved
flash and loaded saved settings before returning the member to use. Keep the
Pico idle and do not edit settings between preparation and continuation.
Reloading the page or restarting the service retains the continuation state.
The controller remains available for status and recovery when startup finds a
retained maintenance hold; that Pico remains unavailable for transmission.

During installation, continuation or unresolved maintenance, the application
is read-only except for firmware status and continuation/recovery. A banner
identifies this state; configuration, device selection, transmission controls,
reboot and shutdown are disabled. The HTTP and WebSocket handlers also reject
mutations, including requests from other tabs. The maintenance hold survives a
failed status read or browser reload. Normal controls return only after the
host confirms that firmware work and its durable hold have ended.

Firmware status refreshes automatically while the selected USB Pico is
available, including after page reload or another tab starts work. An activity
indicator identifies image checking, installation or recovery; monitoring time
and the age of the last successful status read distinguish ongoing work from a
lost connection. Failed reads retry automatically, without repeating an install
or recovery request. A temporary loss of the general Pico status does not cancel
this firmware monitor. Each result is bound to the selected USB serial and host
selection context; changing image choices still requires a fresh image check.

The shared installer exposes no overall percentage or live byte counts. The
activity indicator is indeterminate and does not claim a particular backup,
programming or verification fraction. Read firmware status remains available
while work is running. A manual read shows a reading indicator and successful
read time, even when the update state has not changed. Reads observe host progress;
they do not repeat programming or access the Pico. Monitoring stops when the
page closes or the selected USB connection becomes unavailable.

GitHub API rate limits identify the actual refusal and earliest retry time,
rather than implying that the selected branch has no verified image. Fleet
shows the UTC time, the browser's local time and a countdown. Download and check
image stays disabled during that wait, including after reload or changing
branches. Firmware status keeps refreshing from the Pi without another GitHub
request. When the wait ends, the button becomes available; another download
still needs an explicit click. A minimum one-minute wait is identified when
GitHub supplies no usable retry header. Limits on optional additional CI status
do not invalidate a verified checked image. GitHub response bodies, addresses
and credentials are not exposed in the error.

A refusal before programming reports that no firmware was written. Safe error
text is shown beside the firmware controls; paths and backup contents remain
private. The core retains the failure reason and stage in its private
transaction record so an administrator can diagnose a failed preflight without
guessing or repeating a write.

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

A verified update negotiates a fresh WTP session and confirms the exact new
boot, device identity and firmware version against both USB control channels.
The previous session's boot-change protection remains latched. If programming
is verified but Pi reconnection is unconfirmed, the page identifies that state;
Recover this update retries only reconnection and does not reflash the Pico.

Recover this update can finish a retained core transaction or retry Pi
reconnection after verified programming. Host loss before the Pi receives the
private transaction path may require administrator recovery: inspect
`/var/lib/wsprrypi/pico-usb` over the host console, bind the correct serial and
use the core's explicit `recover --transaction ... --device ... --board pico2_w
--yes` command. A record without a completed backup is not permission to program.
Do not delete a marker to bypass an unidentified state or restore another
board's backup. Deliberate BOOTSEL reconnect may be necessary. Browser status
omits private paths and never contains flash/credential bytes.

## Independent installer updates

`config/pico-usb-installer.json` declares installer contract 1 and an update
channel. It contains no WsprryPico commit or file checksums. The protected Pi
adapter understands the result, transaction and firmware-manifest contracts;
compatible shared installer fixes are published by WsprryPico independently of
Pi releases. The installer channel is independent of firmware image selection.

**Download and check image** checks the compatible installer channel before
planning an image. Successful update checks are cached for ten minutes. The
Pi accepts only an exact public version release, complete uploaded assets,
SHA-256 checksums, matching source tag and successful installer CI provenance.
An incompatible contract, unsupported Python version, changed payload or failed
CI cannot replace the installed copy. The first installation requires a valid
published bundle; a refresh failure retains the previous verified copy and
reports the reason beside the firmware controls. GitHub retry guidance is
honored without retrying early. No GitHub credentials are loaded.

A checked image retains its installer identity. Installing it does not check
for or substitute a newer installer. Before the first possible firmware write,
the adapter persists an immutable installer/schema/receipt snapshot in the
private transaction area. Recovery reads that snapshot and the retained image
offline, even after the installer channel advances or its general cache is
removed. Existing transactions use the frozen installer retained when the
adapter was migrated. Root ownership, file protection, symlink refusal and
checksum verification apply before execution; verified bytes are executed
without a Python bytecode cache. Adapter replacement and entry processes use a
shared host lock. Package replacement refuses outstanding Pico maintenance.

## Storage limits

The Pi keeps three recent image-check downloads, two completed firmware
transactions in total, and up to eight pre-programming refusals. Completed and
unchanged histories expire after seven days. These are global limits across
installer versions. Unresolved recovery records and backups are never evicted
to make room; eight unresolved transactions block another installation.
Installer objects are retained only for the active selection, those three
image checks and the legacy recovery copy. Private snapshots survive while
their transaction survives. Same-board local-build references retain their
existing maximum of 32 boards and remain separate from saved settings.

## Package lifecycle

`scripts/pico_usb_deploy.py` installs the protected entry, distribution manager
and channel policy. It obtains a compatible public installer on first install
and validates all bytes before publishing executable content. Migration keeps
the previous exact installer for legacy recovery. An explicit maintainer
`--development-source` deployment remains labelled `unpublished-working-tree`;
it never impersonates a verified CI publication. Removal retains private
recovery material and immutable installer snapshots.

The package creates private root-owned backup/control/cache directories,
`dialout` USB permissions for WsprryPico CDC and RP2350 BOOTSEL, and a setgid
per-device lock directory restored by tmpfiles at boot. Permissions apply to
new USB enumeration after rules reload; reconnect a cable when necessary.
Python 3.10+ and a USB-enabled pinned picotool 2.3.0 are required. It does not
install an SDK, compiler or picotool, or change unrelated backend settings.

## Validation and documentation impact

BOOTSEL migration software checks cover preparation without programming,
same-board continuation, early continuation retry, changed-port refusal,
disabled-schedule admission, backup/cache/image failure, reserved-byte
readback and postboot semantic mismatch. All 36 shared installer tests pass on
macOS and Linux; six package/adapter tests, the firmware UI unit checks, the
Linux installer adapter checks and 878 hardware-inhibited production WTP
checks pass. The release application, UI and hash-bound development core are
deployed on wspr4. Host settings bytes remained unchanged. Pico C's physical
BOOTSEL migration completed with a verified full backup, preserved reserved
flash and saved settings, exact firmware/boot identity, idle output and a cleared
maintenance hold. A connection-only recovery retained that same boot and did
not reflash the Pico. The BOOTSEL instructions, status-read acknowledgement
and reconnection feedback were rendered at desktop/mobile sizes in light/dark;
the operator workflow and final verified state were also checked in Firefox.
No RF transmission was performed. Broader physical failure/recovery and
multi-board qualification remain separate from this result.

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
