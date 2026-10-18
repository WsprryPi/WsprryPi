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

Read firmware status remains available while work is running. A manual read
shows a reading indicator followed by the time of the successful status read,
even when the update state has not changed. The read observes host progress;
it does not repeat programming or access the Pico.

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

An explicit maintainer deployment can pass `--development-source` to
`scripts/pico_usb_deploy.py`. It installs the reviewed unpublished shared core
and schema from that source directory, records their exact hashes and labels
the deployment `unpublished-working-tree`. Ordinary package installation
continues to use the published revision in the descriptor. This development
deployment does not change firmware CI selection or represent a published
installer revision.

The deployment is pinned to integrated Pico revision
`cc12ca45eb66a95e518b81119e3d6ca546db1547`, with exact core/schema hashes in the
descriptor. Its installer version is `0.1.0`; firmware selection still defaults
to main, with explicit devel selection. The reviewed development deployment on
wspr4 completed Pico C's USB migration to devel image
`0.1.0-ci.39145255987.1` from source
`f6ed8a208e2919a00b7ced044349fd45336f8760`. Reserved bytes and public saved state
passed preservation checks, and the Pi reconnected to the verified new boot.
This board-specific result does not establish package release readiness or RF
qualification.

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
