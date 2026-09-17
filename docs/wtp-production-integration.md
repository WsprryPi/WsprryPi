# WTP production integration

WsprryPi can explicitly select a Pico WTP/1 endpoint using persisted settings
and `--backend wtp`. The parent application owns the complete-job scheduler,
worker, Linux USB CDC or authenticated TLS network adapter and explicit recovery lifecycle.
See [network configuration and credentials](wtp-network.md) and
[shared browser API](wtp-browser-api.md) for Phase 11.1. The web interface
can reveal Pico settings through a browser-console boolean. This is
implemented software integration; physical USB, target lifecycle, timing and RF
qualification remain separate acceptance work.

## Selection and configuration

On Signal Setup, open the browser's developer console and assign:

```js
window.WtpUi.developmentControlsVisible = true;  // Show Pico controls.
window.WtpUi.developmentControlsVisible = false; // Hide Pico controls.
```

The boolean updates the Transmitter tab immediately; there is no visible
visibility switch. Read `window.WtpUi.developmentControlsVisible` to check it.
Only boolean values are accepted (not strings such as `"false"`). It defaults to
false on every page load and lasts only for that page. Reloading hides the
entire Pico panel, including **Use Pico**, until the console boolean
is explicitly set to true again. Previously saved browser visibility preferences
are ignored; this boolean does not read or write browser storage.

**Use Pico** is the separate, persisted backend selection. Hiding the
controls preserves saved selection and field drafts; a passive notice identifies
Pico when it remains selected. Visibility changes do not save configuration,
enable transmission, select another output, reconcile the device or clear
recovery faults. Status polling continues while Pico is selected, even when its
controls are hidden. This console preference is a development convenience, not
an access-control boundary.

An explicit USB endpoint requires all of the following:

| Setting in `[WTP]` | Meaning |
| --- | --- |
| `Transport` | Explicit `usb` or `network`; omitted legacy values mean `usb`. Network fields are documented in the network guide. |
| `Endpoint` | Selected WTP character-device path under `/dev/`; stable by-id aliases are accepted after identity checks. |
| `USB Serial` | Exact serial string, including any leading zeros. |
| `USB Vendor ID` / `USB Product ID` | Decimal integers from 1 through 65535 for the selected device. |
| `Device ID` | Expected WTP identity: 32 lowercase hexadecimal characters. |
| `Start Uncertainty ns` | Maximum start uncertainty, 1–1000000000 nanoseconds; default 1000000. |
| `Allow Frequency Adjustment` | Default false; explicitly permits the existing CAPS-bound frequency rounding policy. It grants no RF qualification. |

`Operation.Transmit Backend = wtp` selects these settings. The default inactive
WTP section has empty identities and zero USB IDs; existing configurations need
no migration. Active incomplete or malformed settings are rejected before
runtime mutation. Settings for inactive GPIO and Si5351 backends are retained.
WTP uses zero host PPM, GPIO output and electrical drive controls. Enabled LED,
amplifier, shutdown and band-selector GPIO, including per-frequency `@` selectors,
are rejected while WTP is selected. Unsupported fades are rejected as well.

The CLI selects the backend with `--backend wtp`; endpoint fields come from the
file supplied by `--ini-file` (`-i`). There are no endpoint-discovery or Console
clock CLI switches. Review the existing file's transmit/boot policy before using
it: backend selection does not itself override an enabled transmission setting.
Keep transmission disabled while selecting and verifying the endpoint.

On Linux the adapter verifies the exact serial, VID/PID and WTP CDC interface
before opening the selected port. It refuses the Console interface. Startup
negotiates HELLO/STATUS/CAPS and inspects ownership/output without CLAIM, LOAD,
ARM or ABORT. It does not adopt foreign or standalone work. A path that has
never negotiated or submitted work can be corrected; an established unresolved
session prevents replacing the backend or endpoint.

## Clocks and complete jobs

Host UTC must be independently synchronized. Linux readiness uses read-only
`adjtimex` status: no unsynchronized/clock-error flag, no `TIME_ERROR`, and a
maximum error no greater than 500 ms. This is host scheduling evidence only.
The Pico must separately have valid UTC, acceptable uncertainty, holdover and
leap state. GET_CLOCK observes that state and does not provision it. Neither
clock check calibrates RF frequency.

The parent prepares complete finite WSPR, QRSS, FSKCW and DFCW jobs using the
existing canonical encoders. One intended WSPR frame is loaded per slot. An
explicitly bounded Tone request is supported by the runtime API; the existing
continuous Test Tone workflow is rejected for WTP. No per-symbol transport control,
local output fallback or implicit infinite repeat is used.

Production jobs dispatch before the requested RF start by a fixed 12-second
preparation allowance, a one-second ARM-submission reserve and the Pico's
negotiated minimum ARM lead. New automatic jobs add a further two-second
submission margin. WSPR uses an eligible 120-second boundary plus one second.
Scheduled non-WSPR work retains the configured repeat grid and advances whole
intervals when necessary to leave enough preparation time. These intervals only
bound CLAIM, complete-job LOAD, validation and ARM; the Pico executes all RF
events locally after ARM. Once submitted, a request's RF timestamp is immutable;
late or invalid admission fails instead of silently moving its start. A
zero-frequency WSPR skip waits on the host and sends no remote job.

Host frequency policy continues to classify WTP combinations as untested. The
existing explicit unqualified-frequency opt-in is required for those jobs;
outside-amateur-band requests additionally require the existing separate opt-in.
Neither setting overrides unavailable modes, endpoint checks, device clock
requirements or unresolved remote output.

## Status, stop and recovery

`GET /api/wtp` returns a copied observation without transport operations or
recovery. Through the application web proxy its path is
`/wsprrypi/api/wtp`. Status includes current observation age, session/device/boot
identity, known output activity, independent host UTC readiness and the
historical last-job result. An absent or failed status response means unknown;
it does not mean unselected, stopped or safe. Observations are not electrical
measurements and idle snapshots do not automatically refresh remote STATUS.

Only a matching authoritative running observation establishes running status.
ARM acknowledgment and local preparation do not emit the legacy RF-start/GPIO
callback. Completion is dispatched on the application thread after the protocol
worker finishes. Successful completion requires matching terminal job evidence
and inactive output. Historical failures remain visible after later cleanup.

Stop requests cancellation and joins the owned worker. Cleanup is bounded and
uses only the tracked job's owned ABORT and an owned, inactive RELEASE. A lost
acknowledgment, lost connection, missing job or changed identity cannot prove
output inactive. Unresolved cleanup blocks subsequent work, backend replacement
and generic fault clearing. A reload may invalidate waiting work; committed
preparation/execution wins the atomic boundary and defers the reload.

**Reconcile Pico** is an explicit idle action. It reconnects the selected endpoint
with the same session, inspects current state and may stop this session's tracked
job during cleanup. The request is `POST /api/wtp/recover` with JSON
`{"operation":"reconcile"}` and follows the existing trusted-LAN write policy.
It never reloads or rearms an uncertain job, steals ownership, or clears device/
boot identity and protocol faults. Status polling never invokes this action.
After recovery, review configuration and explicitly resume through the normal
controls; recovery itself does not enable transmission.

Session history is process-local. Restart creates fresh identities and performs
read-only startup inspection, not durable job adoption. The session tracks at
most 4096 job identities and then requires a safely quiescent restart. Closing
USB or exiting the process is not proof that RF has stopped.

## Builds and validation boundary

The parent application links WTP alongside its configured reusable transmitter
profile and reports it in `--list-backends`. There is no `BACKENDS=wtp` component
profile: the reusable transmitter factory does not own USB sessions. Portable
macOS builds still use `BACKENDS=simulated ANCILLARY_GPIO=0` for that component;
native USB identity checks are Linux-only. TLS software is also exercised on macOS;
production deployment and host UTC admission remain targeted at Linux. The native WTP opener honors
`WSPRRYPI_DISABLE_HARDWARE_ACCESS=1`. Injected clocks/streams are typed C++ test
seams, with no public API or configuration selector.

Hardware-free checks from `src` include:

```sh
make wtp-application-test SUDO=
make wtp-production-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
make semantics-test-portable SUDO=
make wtp-scheduler-interop-test SUDO= PICO_SOURCE=WTP-Client/build/pico-reference
```

The final command requires the pinned reference checkout described in the
[scheduler contract](wtp-scheduling.md). These checks exercise software and
injected endpoint behavior only. They do not validate physical enumeration,
permissions, disconnect/reboot behavior, services, installation, RP2350 timing,
output electrical state, RF calibration or the transmitter chain. Target
acceptance must bind exact host and firmware revisions, selected endpoint and
clock state, route, frequency/mode/duration, connected RF path and stop procedure.

The installer contains the status/recovery proxy mapping; no installation or
service validation is implied by source tests. The separate operator manual
needs corresponding backend, INI, Transmitter-tab and recovery documentation
under its own repository authorization before general operator publication.
