# Controller browser routes

The controller browser verifies the same-origin application `GET /version` before enabling role-dependent controls. Its `host_status` is either `{"role":"transmitter"}` or the closed controller document also returned by `GET /status`: `role`, `controller_ready`, `device_id`, `local_output` (null), and `local_transmission_available` (false). UI build metadata from `version.php` or `ui-version.php` cannot establish the runtime role. A missing, invalid, or unauthorized reply keeps device controls unavailable. A different verified role or controller ID requires a reload. Browser verification does not grant management authority. The backend separately enforces host, origin, network, session, and target-binding checks on each protected request.

## Navigation and deep links

| Entry | Controller behavior |
| --- | --- |
| `/`, `index.php`, `index.php?page=operation` | Shows the controller identity and separate Device selector. Empty inventory, unavailable inventory, and a selected device's unknown or unconfirmed state have distinct feedback. Local transmit, tone, reboot behavior, and host power controls are omitted. |
| `index.php?page=config` and `setup_tab` | Opens Fleet when no managed device is selected. Local form, autosave, and local hardware are unavailable. The verified selected device supplies Signal Setup, Scheduling, Transmitter, and Device I/O. Unsupported resources remain unavailable under the selected device's own capabilities and authority. A local-only tab bookmark cannot enable local settings. |
| `index.php?page=maintenance`, `maintenance.php` | Retains encrypted backup and restore, pairing and removal, and scheduling ownership review for explicitly selected supported Pis. Local station repair or reset, privileged network editing, test tone, and Pi support bundles are omitted. |
| `index.php?page=logs`, `index.php?view=journal\|internal\|both`, `view_logs.php` | Omits the Raspberry Pi journal viewer and host logging settings. A direct page link explains where to view native controller logs. |
| `index.php?page=spots`, `view_spots.php` | Omits local-call-sign WSPR spots. A direct page link points the operator to the transmitting device's spots view. QRSS Plus and the WSPRNet Database remain external observation links. |
| Wsprry Pi Links, theme, footer | Documentation, GitHub, TAPR, WSPRNet Database, appearance, build identity, and read-only update awareness retain their existing functions. |
| Supported aliases and tab state | `maintenance.php`, `view_logs.php`, and `view_spots.php` redirect to their canonical pages with query parameters. Unknown page keys resolve to Operation. The former native `config.php` name is UI metadata and has no standalone handler. |
| PHP support routes | `version.php`, `ui-version.php`, and `ui-manifest.php` serve UI identity. `log_stream.php`, `view_diag_logs.php`, and `fetch_spots.php` are standalone Pi web-host assets outside controller browser routing. Controller artifact and web-host admission must exclude these Pi-only handlers in the separately qualified installer slice. |

## HTTP method and route inventory

The local browser transport classifies methods in `src/application_control/http_contract.cpp`. The host role is enforced by `controller_http_allowed` and `register_request_guard` before application handling. Retirement of selected-member aliases precedes the role refusal. Unrecognized paths or methods can be refused by the listener before reaching either check. Asset routes are owned by the web host.

| Path or resource | Method and controller disposition |
| --- | --- |
| `/`, `/version`, `/status`, `/api/control/v1/status` | GET is read-only. The role document is sourced from `/version`, and the browser skips the local-output status poll. HEAD/OPTIONS are subject to the listener and role method policy. |
| `/api/control/v1/targets` and `/targets/{device}/…` under that prefix | GET inventory, context, status, settings, scheduling, and operation receipts. PUT, POST, and DELETE only reach the selected device's supported resources after protected admission. Includes paired Pi setup, transmit permission, stop, scheduling, migration and review, and enrolled Pico management and resources. A GET does not authorize a later write. |
| `/api/control/v1/host/management-session` | POST establishes a protected browser session. It does not change device settings. |
| `/api/control/v1/host/devices`, `discovery`, `discovery/identify` | GET saved inventory and discovery, POST explicit catalog changes and identity checks. Controller has no `devices/use` local-output selection. |
| `/api/control/v1/host/fleet`, `fleet/operations`, `fleet/actions`, `retirements` | GET legacy history and references, and protected POST for accepted catalog/retirement actions. Legacy Fleet is no execution or recurrence path. |
| `/api/control/v1/host/pairing` | GET state, discovery, and operation receipts. POST window, connect, and removal through the pairing owner. `/api/pair/v1/` uses the existing strictly admitted machine bootstrap methods and credentials. |
| `/api/recovery/` | POST session via the backend. GET current review, jobs, job status, or download. POST review and existing job creation, upload, validation, confirmation, reconciliation, and cancel actions through the separate maintenance owner. Local-output release is unavailable for the controller role. |
| `/config`, `/config/repair`, `/config/si5351-addresses`, `/control/stop` | Refuses direct GET, PUT, PATCH, or POST as applicable to the local configuration, inventory, repair, and stop routes. |
| `/api/network-safety`, `/api/rp1-gpclk-route`, `/api/wtp`, `/api/wtp/recover`, `/api/support-{bundles,intake}` | Local Pi configuration, route, execution, and support actions refuse their GET or mutation methods. |
| `/api/control/v1/host/{config,readiness,configuration-recovery,wtp-endpoint,logging,udp-logging}` and resource suffixes | Refuses local transmitter or Pi host-resource access. Browser Target views use the controller's verified identity instead of fetching Pi readiness. |
| `/api/control/v1/member/*`, `/api/control/v1/network`, `/api/control/v1/capabilities` | Selected-member aliases are retired. The standalone member firmware route refuses GET and POST in controller mode. Target capabilities come from the negotiated target context. |

## WebSocket commands

The controller browser does not connect to the local transmitter WebSocket. A direct admitted `get_tx_state` returns disabled state and no local output. Direct `stop`, `tone_start`, `tone_end`, `bounded_tone`, `wspr_band_catalog`, `reboot`, `shutdown`, `echo`, and unknown commands refuse under `controller_only`. Target commands use the protected HTTP management routes and the target remains responsible for recurrence. The controller implements no local fallback.

## Verification

`WsprryPi-UI/tests/controller_mode_browser_test.js` starts the controller-profile production main and separate frontend against private SQLite state and hosts the shipped PHP through a loopback proxy. It covers unauthorized and malformed status, a starting host, empty enrollment, local-action and origin refusal, deep links, keyboard navigation, and responsive captures. The network pair invokes `pico_target_editor_browser_test.js` against two production software Pico peers for explicit target selection, separate identities, draft isolation, and authoritative readback after a lost reply. Neither fixture operates a physical transmitter or provisions an installed service.
