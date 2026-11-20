# Local application control

The backend owns the private database and settings coordinators. Local commands connect to its Unix socket and verify the server's operating-system identity. The backend derives the caller's identity from the socket. Requests cannot supply an authorization principal or choose a database path. Commands do not retry mutations after a missing reply; read the settings again to establish the saved result.

The advanced-settings command uses the current configuration revision:

```sh
sudo wsprrypi --settings read
sudo wsprrypi --settings write --expected-revision 7 --set 'CW.Fade In Ms=25' --set 'CW.Fade Out Ms=25'
```

Use the revision returned by the read, rather than the example value. Multiple `--set` arguments form one validated change. A stale revision or invalid value leaves every saved setting unchanged. The command requires the running backend; it does not provide offline settings editing. `--settings --help` lists the ten advanced fields. A controller exposes only debug logging and the two listener ports.

Readback distinguishes saved and active values. Listener changes require restart when an old listener remains bound. An explicit startup override can differ from the saved value and must be removed to use that saved value on a later startup. Advanced edits retain the independent network-safety policy and saved startup loop and iteration preferences. Ordinary station and schedule settings remain in the existing UI.

`--runtime-directory` selects an explicit local socket directory for isolated development processes. Installed commands use `/run/wsprrypi`. This option does not select persistent state or weaken peer verification.

The backend owns pairing supervision and support jobs independently of HTTP listener stop and restart. Local control, observational events, browser connections, and streamed HTTP resources have separate socket endpoints and bounded worker pools. Requests are bound to the backend boot, a transport-created connection identity, and a strictly increasing per-connection sequence. A stalled event or download consumer cannot occupy a control worker. Event loss requires authoritative resynchronization and never acknowledges a command or clears a Stop hold.

Browser commands retain the original backend origin/network checks. A connection owns only the Test Tone generation it started; disconnect cleanup cannot cancel a newer generation or unrelated autonomous scheduling. HTTP requests select closed application operations and retain existing owning route admission. Request bodies and response resources cross IPC in at most 64 KiB chunks. Support downloads keep their existing digest/file validation and acknowledge full delivery only after the outer HTTP writer reports success; reading all bytes into the web process is insufficient. The web process receives neither private paths nor open database/key-store access.

`wsprrypi --web-service` runs the separate HTTP and browser WebSocket process. It obtains its role, identity, process listener settings, and backend boot through verified IPC. It does not open SQLite, select a transmitter, provision an identity, or own device TLS connections. Apache continues serving the existing assets and proxy URLs. Web restart leaves the backend and its persistent owners running. A cached, secret-free listener document outside the private state generation retains the maintenance endpoint while the backend is unavailable; ordinary requests report backend unavailability rather than synthesizing configuration.

The Linux service topology uses root `wsprrypi.service` and unprivileged `wsprrypi-web.service`. The browser unit has a weak startup dependency on the backend, with no stop propagation. Its service sandbox excludes private application state and physical devices. The backend publishes group-accessible sockets under `/run/wsprrypi`; its database and credential state remain private. These unit definitions have installer-fixture coverage, not installed-service qualification. The active listener contract is captured once per backend process; saved listener edits take effect after backend restart, and the existing web process then adopts the admitted endpoints. Apache proxy configuration must continue to match those ports.

The independent `wsprrypi-maintenance.service` owns encrypted backup and restore jobs; see the [recovery contract](../application_recovery/README.md). It survives backend replacement and exposes no generic service executor. `sudo wsprrypi --diagnostic-export` returns a closed supported-settings export through administrator IPC, including advanced saved values and observed listener ports. It excludes raw databases, credential stores, receipts, and inactive recovery evidence. A changed export or unavailable backend returns an explicit unavailable document and exit status 3 without partial settings or guessed defaults. The support collector uses this interface and separately observes the backend, browser, and maintenance services.

Actual-process checks cover IPC, HTTP/WebSocket route admission, web restart, unchanged backend state, generation-owned tone cleanup, and backend exit while the web endpoint remains available with truthful unavailability responses. Backend shutdown drains local request workers before destroying their domain owners. The connection-owned tone check uses the existing typed execution-suppression fixture and qualifies reservation/cleanup semantics, not transmitter execution. These interfaces do not qualify installed services, physical timing, or RF output.
