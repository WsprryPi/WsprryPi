# Pico USB web erase and restoration test

Date: 2026-10-19. Result: **passed**. Evidence collection completed at
2026-10-19 12:29:46 UTC.

The deployed WsprryPi web erase flow removed all writable flash from Pico C.
An independent readback confirmed that every byte was blank. The original
firmware and persistent settings were restored afterward, and the selected
Pico returned ready, idle, unowned and output-inactive.

## Tested identity and deployment

| Item | Observed value |
| --- | --- |
| Host | `wspr4`, Debian 13, Linux ARM64, Raspberry Pi 4 Model B Rev 1.1 |
| Board | Pico C, Pico 2 W / RP2350, 4 MiB flash |
| USB serial | `1545B6EFBF78028C` |
| Stable device ID | `c2a3a2a314c948b50a8562a1aeb69669` |
| Physical USB parent | `1-1.3` |
| WsprryPi application and UI source | `8b7b7fee5dc334f753ec06b1b635108893967670` |
| Published USB installer source | `90bb95ebd054fa9084185e5ef9ed79528609c014` |
| USB installer version | `0.1.1` |
| USB installer release | `usb-installer-v1-devel-39249064224.1` |
| Original and restored Pico firmware | `0.1.0-ci.39155533863.1` |
| Original and restored firmware source prefix | `0a7c05d1c41b` |

The application was built on wspr4 from an isolated export of the committed
Pi source, with matching build metadata. Unchanged compiler objects were
reused only after comparing their dependency contents and freshness; changed
objects were rebuilt. The deployed adapter and web assets came from the same
commit. The shared installer was downloaded from its published release and
verified against its receipt, checksums, source tag and successful CI.

The running application reported commit `8b7b7fe` and a clean build. The
wspr4 working checkout was left unchanged at `8766d3260b9197e34733c8aa2bd1b1c45d0eb773`;
the deployment used the isolated export rather than that checkout. An initial
UI publication attempt refused the export because its CLI required Git
metadata. The deployment rolled back, then succeeded using the publisher's
explicit-source API. Application and chrony configuration were preserved.

## CI checked before deployment

All three runs completed successfully for the exact sources above:

| Workflow | Run |
| --- | --- |
| WsprryPi non-hardware validation | [39249067008](https://github.com/WsprryPi/WsprryPi/actions/runs/39249067008) |
| WsprryPico software and firmware | [39249064303](https://github.com/WsprryPi/WsprryPico/actions/runs/39249064303) |
| WsprryPico USB installer | [39249064224](https://github.com/WsprryPi/WsprryPico/actions/runs/39249064224) |

## Procedure and observations

1. Confirmed the selected serial and stable identity. Pico C was idle and
   unowned, with no active output or enabled standalone schedule; its saved
   standalone configuration was unconfigured.
2. Opened **Setup → Fleet → Pico firmware over USB → Erase this Pico** on
   `http://wspr4.local/wsprrypi/`. The confirmed preparation request reached
   the production member-firmware endpoint and returned `awaiting_bootsel`.
   No flash was erased during preparation. The durable host maintenance hold
   disabled configuration, selection, transmission, reboot and shutdown.
3. Used the existing guarded USB management transition as a test fixture to
   enter BOOTSEL on the same board and USB parent. Retained and verified a
   private 4,194,304-byte restoration image. This fixture backup was separate
   from the erase operation; the erase transaction created no credential
   backup and contained only `blank.bin` and `transaction.json`.
4. Selected and confirmed **Erase after BOOTSEL** in the browser. The deployed
   production endpoint resumed the retained erase transaction with installer
   source `90bb95e`. The UI displayed erase/verification progress, kept other
   controls locked and refreshed status without replaying the mutation.
5. The web result reported `ok=true`, `operation=erase`, `erase_verified=true`
   and `erased_bytes=4194304`. The browser displayed **Installed none (flash
   erased)** and verified completion. The host maintenance hold was cleared;
   the board remained in BOOTSEL.
6. Independently read the entire range `0x10000000` through exclusive
   `0x10400000` using USB-enabled picotool 2.3.0, verified that read against
   the device, and compared all 4,194,304 bytes with `0xFF`. The comparison
   passed, including the firmware and persistent-data areas.
7. Stopped the host service for restoration. Loaded the private original
   image, verified programming, and independently read back the entire flash.
   The restored 4 MiB matched the original image byte-for-byte before reboot.
   After normal reboot, the firmware version, source revision and saved
   settings semantics matched the pre-test snapshot. Restarted the deployed
   service and confirmed the final state below.

Desktop (1280 × 900) and mobile (390 × 844) browser views were inspected during
the test. The mobile document fit its viewport without horizontal overflow.
The verified erase result and restored firmware were visible in the panel;
no browser console warnings or errors were recorded. The browser automation
reported a preparation-confirmation error, so the accepted server state was
checked before continuing; the preparation request was not repeated.

## Integrity and restoration results

| Check | Result |
| --- | --- |
| Independent blank readback SHA-256 | `cd3517473707d59c3d915b52a3e16213cadce80d9ffb2b4371958fb7acb51a08` |
| Deployed application SHA-256 | `a570783ea53b6b3eddd7c6fe73f90a4e88b398cfce36759291ba5f31152e3d93` |
| Published installer receipt SHA-256 | `7aec11edec6b38097f33cc38b17086d88ff26c872d79535d818b52078826150d` |
| Original/restored full-flash comparison | Byte-identical, 4,194,304 bytes |
| Firmware and saved settings after reboot | Match pre-test snapshot |
| Restored boot ID | `00c66e7ca0d344369e5b1e97b0cf969e` |
| Host service | Active and ready |
| Pico state | Idle, unowned, output inactive; no recovery or maintenance hold |
| Host chrony service | Active; not changed by this test |

The following configuration hashes matched before deployment and after
restoration:

| Preserved file | SHA-256 |
| --- | --- |
| `/usr/local/etc/wsprrypi.ini` | `8826b8392de778e71bda9d1e58567e6cae460e3b8c8600ccc3f3bd14fb171177` |
| `/etc/chrony/chrony.conf` | `c88d5bb970adbf75809dc17a319ee579486686077f82395c2d152ed4272a2513` |

The unrelated NTP/μChrony source work was not deployed or modified. The local
WsprryPico checkout remained at `b76d3cb8e2a59bc5db2e0a2f35488b2d3842be0c`
through this hardware test. Private flash images and configuration snapshots
were retained outside source control; their contents are not part of this
record.

## Coverage limits and documentation impact

This qualifies the successful deployed web erase and restoration path on the
identified Pico C. The BOOTSEL handoff used the guarded USB fixture; a physical
unplug/hold/replug sequence was not exercised. No power-loss fault injection
or RF jobs were performed. RF engine behavior, clock accuracy, timing and RF
output were not qualified by these BOOTSEL flash operations.

The existing [USB installer documentation](../pico-usb-installer.md) matched
the observed operation. This record adds test evidence without changing the
product workflow or operator instructions.
