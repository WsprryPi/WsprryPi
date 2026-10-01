# Managed RP1 and Fleet workflow fix

2026-10-01. Executed [the comprehensive prompt](wtp-managed-rp1-fleet-fix-prompt.md)
on `devel`, from base `6a32789cfb84881df9e6839b27a9538f69e7bed1`.

**Implementation and automated/browser checks passed. Final concurrent physical
frame/RF acceptance remains pending the stopped combiner rotation.**

## Changes

- Managed local WSPR, QRSS, FSKCW and DFCW can prepare supplied transient RP1
  confirmation before resolving the route gate. Existing local admission,
  startup/reload, provider, request authorization and cleanup gates remain.
- INI and HTTP transactions retain process launch confirmation; it is not
  serialized into INI or exposed in public configuration.
- Reapplying Enable preserves an already enabled local schedule. A Test Tone
  or disabled schedule awaiting cleanup still holds new Enable admission.
- The installed-service validator admits one narrowly validated confirmation
  argument on the canonical command and rejects ambiguous commands.
- Fleet Pause/Remove uses a named in-page confirmation with Cancel focus,
  frozen revision, duplicate protection, cancellation on hiding/departure and
  explicit conflict feedback. Successful reconnection clears stale fetch errors.

Parent application paths modified: scheduling, configuration, launch help and
`src/wtp_endpoint/authority.cpp`; service companion and tests under `scripts/`.
The UI component changes are confined to `WsprryPi-UI`. No reusable
`src/WSPR-Transmitter`, `WTP-Server`, `WTP-Client` or external provider source
was modified.

## Validation

Commands ran from `src` unless otherwise stated. Every final check below passed.

| Check | Command / evidence |
| --- | --- |
| Full Linux semantics on wspr5, libgpiod headers, Avahi enabled | `WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j3 semantics-test SUDO=`; [log](wtp-managed-rp1-fleet-results/linux-avahi-final2.log). Includes backend runtime/cleanup semantics with hardware access disabled. |
| macOS portable semantics | `make -j3 semantics-test-portable SUDO= CXX=/usr/bin/clang++` with `SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk`, `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`, `MACOSX_DEPLOYMENT_TARGET=27.0`; [log](wtp-managed-rp1-fleet-results/portable-semantics.log). Explicit simulated subset. |
| Final authority repair on macOS | Same toolchain, `make wtp-pi-authority-test SUDO= CXX=/usr/bin/clang++ BACKENDS=simulated ANCILLARY_GPIO=0`; [log](wtp-managed-rp1-fleet-results/portable-authority.log). |
| RP1 route integration | `make rp1-gpclk-route-runtime-wiring-test rp1-gpclk-route-service-test SUDO=` on wspr5. |
| Service companion | `python3 scripts/tests/route_application_test.py` (10 tests), `python3 scripts/tests/runtime_reconcile_test.py` (20 tests). |
| UI standalone | In `WsprryPi-UI`, `npm run test:unit`; [log](wtp-managed-rp1-fleet-results/ui-unit.log). |
| Rendered UI regression | In `WsprryPi-UI`, `node tests/wtp_pi_fleet_ui_integration_test.js`; [log](wtp-managed-rp1-fleet-results/ui-browser.log). Includes local takeover mocks, conflict drafts and confirmation failure cases. |
| Impeccable | Desktop/mobile rendering and actual controller dialog review; static detector returned `[]`. |
| Evidence/source checks | Python compilation for recorded harnesses, JSON parsing, exact changed-source hash comparison against the native stage, whitespace and staged diff checks. |

Initial macOS attempts failed with the default CLT SDK/linker combination.
The installed Xcode SDK/toolchain selection above resolved that host limitation.
Local loopback fixtures required sandbox escalation. Expected negative-case
error/warning output remains in the semantics logs; the final suite exited zero.
Dependency installation reported a processor-microcode check limitation; no
service restart was required by that package operation. No CI result is claimed
for the new commit in this report.

## Live results

| Check | Outcome / limit |
| --- | --- |
| Ordinary managed wspr5 service | Updated native binary/helper/UI installed with private rollback archives. Actual process arguments and hashes verified. |
| Native repeated-Enable scheduling | Passed: normal managed RP1 WSPR preparation survived repeated HTTP/INI Enable commits, retained local admission and was stopped before its RF window. [Observations](wtp-managed-rp1-fleet-results/managed-wait-check.json). |
| Actual browser Resume | Passed on all five existing physical assignments in the bounded attempts. |
| Actual browser Pause | Cancel preserved the future schedule with no mutation; Confirm sent exactly one guarded POST and paused it. The future fixture prevented RF while wiring was pending. Original definition recreated paused through the API so the canceled far-future slot does not delay the next campaign. |
| Actual browser Remove | Cancel preserved the assignment. Confirm removed the paused Pico B assignment; its saved definition was recreated paused through the guarded API. This creates a new assignment context. |
| DNS-SD after deployment | Passed advertisement and discovery on the final Avahi-enabled build. All six physical members visible; this is not link-loss/address-change acceptance. |
| Final output state | Independent HELLO/STATUS on all six endpoints: empty, unowned and inactive. wspr5 local Enable off, five assignments paused, no in-flight work or unknown output. |
| Final full frame/RF pass | Pending: last repair has not completed a normal 110.592-second frame beside all five targets or observed the second SDR group. |

Two earlier live attempts were retained privately on wspr5. In `run1` the running
process lacked the new confirmation argument. In `run2` repeated Enable canceled
the local wait; the observer stopped that attempt before the selected slot.
Neither contributes a successful local frame. Earlier successful remote batches
are documented separately in [the historical report](wtp-concurrent-fleet-report.md).

The supported provider update used exact source
`924c7e546ab82a40497339c4baeb813f95f3408e` and the unchanged installed kernel
modules. Changing the companion's integrity-bound hash required neutral
recovery/removal and reviewed redeployment/activation plans. GPIO20 was restored
idle. The [provider receipt](wtp-managed-rp1-fleet-results/provider-refresh-receipt.json)
and [source manifest](wtp-managed-rp1-fleet-results/source-manifest.json) retain
identities. The native binary is built from that base plus the recorded source
hashes; its pre-commit version label still contains `6a32789`.

## Cleanup and remaining work

The temporary confirmation drop-in was removed. The normal canonical service
was restarted and its actual command verified. New tested binary/helper/UI and
the verified provider binding remain installed. GPS/PPS and receiver calibration
were preserved. Avahi build headers remain installed. Private rollback archives
and raw IQ remain outside the repository on wspr5; only compact evidence is
committed. The approved Fleet frequency override remains set while this campaign
is pending, with all schedules paused and local Enable off.

Next acceptance step: while outputs remain stopped, retain the SDR tap and
connect **wspr2 GPIO4, Pico A GP2 and wspr5 GPIO20**. After the operator confirms
the rotation, restore the temporary bounded launch confirmation, verify actual
arguments and run the final three-slot mixed Fleet plus one normal local WSPR
frame. Remove the temporary confirmation and return to paused/off state again.

Wider outstanding campaigns remain: eight isolated simulated targets, sustained
recurrence/reconnection, both local takeover choices through a browser connected
to actual Pis, and discovery link loss/address changes/multihomed recovery/cache
expiry. They are not acceptance claims for this focused fix.

## Review and Documentation Impact

The [adversarial review](wtp-managed-rp1-fleet-results/adversarial-review.md)
records findings, repairs and reassessment. No additional actionable source
defect was identified after repairs; final physical acceptance remains open.

- Updated: `docs/runtime-route-workflow.md`, `docs/wtp-fleet.md`, execution
  prompt, this report, historical-result link and compact evidence.
- Reviewed unchanged: `docs/wtp-pi-operation.md`, `docs/simulated-backend.md`
  and sibling operator pages `docs/User_Interface/index.md`,
  `docs/User_Interface/Setup/index.md`,
  `docs/Advanced_Operations/rest_api.md`,
  `docs/Advanced_Operations/ini_configuration.md`, and
  `docs/Command_Line_Operations/transmitter_backends.md`.
  Existing independent control/local precedence and selected-output semantics
  remain accurate; the transient RP1 developer launch is documented here.
- Required later: update operator screenshots if a selected screenshot depicts
  the replaced native dialog. No currently affected operator screenshot was
  identified. The sibling documentation repository was not modified by this fix.

The implementation, tests and evidence are committed together on `devel`;
the completion message records the commit, push parity and final working tree.
This report explicitly leaves physical acceptance pending and claims no release
readiness.
