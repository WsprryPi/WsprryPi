# Wsprry Pi UI

This is the Bootstrap UI for [Wsprry Pi](https://github.com/WsprryPi/WsprryPi/).
It is installed with the current application installation.

## Browser dependencies

The UI serves its browser libraries and fonts locally, including the standalone
diagnostic-log viewer. The main interface uses Bootswatch Zephyr; the standalone
viewer uses plain Bootstrap CSS and does not load the shared application shell.

| Asset | Version |
| --- | --- |
| jQuery, full browser build | 4.0.0 |
| Bootstrap / Bootswatch Zephyr | 5.3.8 |
| Popper, included in the Bootstrap bundle | 2.11.8 |
| Bootstrap Icons | 1.13.2 |
| Font Awesome Free, CSS and WOFF2 | 7.3.1 |
| Barlow Semi Condensed, weights 500/600/700 | 1.422 |
| Source Sans 3, weights 400/500/600/700 | 3.052 |

Use current desktop and mobile browsers. jQuery 4 drops older browser versions
supported by jQuery 3; its exact dependency-level support policy is documented
in the [jQuery 4 migration guide](https://jquery.com/upgrade-guide/4.0/).
That policy is not a qualification of the complete console on every listed
browser. The browser fixtures use Chromium.

`ws` 8.22.1 is a development-only dependency for browser fixtures; it is not a
browser library or a requirement on the installed UI host. Node.js remains
required only for development/testing, with the existing minimum of 18.

Asset updates participate in the existing packaged UI identity and cache-key
mechanism. An already open page continues using its loaded scripts until the
operator accepts the existing refresh prompt or reloads it. Save or otherwise
resolve unsaved drafts before choosing a refresh; a page reload does not promise
to preserve every transient draft. Dependency updates do not change settings,
storage keys, authentication, or control-session policy.

Distribution sources, licenses, and local asset-path adjustments are recorded in
[`data/vendor/THIRD_PARTY_NOTICES.md`](data/vendor/THIRD_PARTY_NOTICES.md).
When updating vendored CSS, keep fonts and CSS together and run `npm test`;
the vendor-resource check rejects missing or external CSS resources. Run
`npm run test:browser` to exercise actual assets, AJAX, Bootstrap interactions,
stored preferences, and synthetic log streams. Optional `JQUERY_MIGRATE_PATH`
points the dependency browser fixture at an official development Migrate 4
script for a temporary browser-response-only migration check. The normal suite
runs without Migrate. `DEPENDENCY_EVIDENCE_DIR` selects a screenshot directory
for the standalone viewer.

Apache and PHP are distribution-managed dependencies selected by the installer.
Their installed versions and security-backport status must be checked on each
serving host, including the PHP web SAPI rather than only PHP CLI. See
[`../scripts/README.md`](../scripts/README.md) for the existing PHP security
inspection and mitigation guidance. Browser-library updates do not update or
qualify those server packages.

## Packaged UI manifest

`scripts/generate_ui_manifest.py` generates and validates the schema-v1,
content-addressed manifest used to identify a packaged UI artifact. Generation
is build-time infrastructure. The normal installer copies the exact UI source
into a sibling staging directory, generates and validates the manifest there,
confirms that the staged installed and packaged identities match, and only then
publishes the complete directory tree. Failed staging or validation does not
replace the live UI.

The identity covers every regular file below the selected UI root except the
top-level runtime `cache/` and `backups/` directories, `ui-manifest.json`
itself, and `.DS_Store` metadata. Symbolic links and unsafe or non-normalized
manifest paths are rejected. Each record contains a normalized relative path
and a SHA-256 content digest; timestamps, permissions, ownership, and traversal
order are not identity inputs.

Generate and then validate a manifest with:

```sh
python3 scripts/generate_ui_manifest.py \
  --ui-root data \
  --output /tmp/ui-manifest.json \
  --source-commit "$(git rev-parse HEAD)" \
  --application-version 0.0.0
python3 scripts/generate_ui_manifest.py --validate /tmp/ui-manifest.json
```

Run the focused regression suite with `npm run test:manifest`.

The same module provides the read-only `classify_installed_ui()` comparison
layer. It validates the immutable packaged manifest, calculates the identity
of the covered files currently present, and returns `packaged`,
`locally_modified`, or fail-closed `unknown` state with deterministic modified,
added, and missing path lists. Classification does not alter installed files.

## Installed UI identity

The UI-owned `/wsprrypi/ui-version.php` endpoint compares the installed UI to
the immutable manifest and reports its installed and packaged identities,
classification state, and modified, added, and missing path lists. The
`/wsprrypi/ui-manifest.php` endpoint exposes the validated packaged manifest.
Both responses disable caching.

Every rendered page embeds the installed identity and uses it as the cache key
for UI assets. These endpoints are ordinary Apache/PHP UI resources; they are
not proxied to the running service. `/wsprrypi/version` remains the service
version endpoint.

An open page polls the UI-owned identity endpoint. A changed installed identity
prompts once for a refresh and supplies that identity as the refresh cache key.
A page that loads with the requested identity has converged; a mismatch instead
shows a persistent consistency diagnostic and suppresses further prompts. A
stable packaged or locally modified installation does not prompt, and service
or executable version changes do not participate in UI refresh decisions.

The published manifest is read-only and owned outside the Apache/PHP runtime
account. Top-level `cache/` and `backups/` content remains excluded from the
identity and may be runtime-owned without changing the packaged classification.

Before replacing a locally modified installation, the publisher creates a
unique verified backup below `/var/backups/wsprrypi/ui`. It preserves modified
and added files plus the prior manifest, and records missing files in
`modification-report.json`. If prior state is unknown, it backs up the complete
covered UI tree. Backup failure prevents replacement; customizations are never
merged automatically. `--fail-on-ui-modifications` makes the installer refuse
replacement instead. The installer relists the affected files, backup and
report locations, and actual replacement status as its final output block.

### Fleet access

The Setup > Fleet tab groups known and nearby WTP devices, connection and status
controls, member management, and output schedules. It is available on every page
load without a console command or visibility preference. Browsing the tab does
not apply endpoint settings or enable transmission. Profile application, member
saves and schedule changes retain their explicit controls.

### Pico finite CW messages

When Pico is selected, the CW message form shows the exact calculated integer-
nanosecond duration, character count out of 32, event count against the connected
device's limit, and the finite-job duration ceiling (at most 60 minutes). Spaces
count toward 32; invalid drafts remain intact. Duration, event and message-length
errors are distinct, and a refreshed smaller capability updates validation.
Other backends retain their existing message limits and duration display.

The calculation quantizes timing fields to nanoseconds before summing events,
matching the parent WTP request builder; QRSS/FSKCW dash length is three quantized
dots. One repetition is a complete native job. The existing repeat interval is
scheduler spacing, not permission to exceed a device's finite-job duration.
Local Chromium tests cover both desktop/mobile layouts, 31/32/33 and spaces,
exact duration boundaries, smaller CAPS and fractional timing. They use simulated
controller responses; live-device R3 acceptance remains separately required.

### Browser fixture process ownership

Chromium fixtures must use `spawnIsolatedProcess` and `stopIsolatedProcess`
from `tests/browser_fixture_process.js`. On POSIX this owns a private process
group, so shutdown also retires descendants that outlive the browser leader.
Use `spawnFixtureChild`, `waitForSpawn` and `stopChild` for the PHP fixture.
Create children inside protected cleanup, attempt every cleanup step, and
remove the profile only after the browser group has stopped. A successful
workflow followed by failed cleanup is a failed test. Do not replace this with
PID-only termination, process-name kills or ignored removal errors.

The PHP fixture router supplies a synthetic, read-only `GET /api/control/v1/status`
response with `host.firmware_maintenance: false`. Wait for
`PicoFirmwareSafety.locked === false` before exercising controls, including
after navigation. Keep the production maintenance guard active; the router
refuses status mutations, firmware actions and other application endpoints.

Run `npm test` and `npm run test:browser` from this component. The process
regression covers an exited leader with a stubborn writer and verifies that an
unrelated sentinel survives. `node tests/wtp_pi_fleet_ui_integration_test.js`
exercises the separate Fleet browser fixture. These use synthetic local
controller responses; an online test-network interface is needed for workflows
that honor `navigator.onLine`, even when external access is disabled.

## Development browser fixtures

Run `npm ci --ignore-scripts`, `npm test`, and `npm run test:browser` from this
component. Browser tests require PHP CLI, Node.js, Chromium, and the POSIX `ps`
utility for owned process-group verification. `CHROME_BIN` selects an explicit
browser; the support-intake fixture also retains `CHROMIUM_BIN`. Fixtures use
loopback services and synthetic application responses, not an installed service.

`tests/browser_harness.js` shares `launchBrowser`, `CdpClient.evaluate`, bounded
`waitFor`/HTTP probes, `settleRendering`, `createBrowserProfile`, and
`finishFixture`. Keep scenario assertions, allowed confirmation messages, URLs,
and meaningful operation budgets in the caller. Navigate the returned owned
page explicitly and wait for its required scripts/state. Startup discovers its
own ephemeral endpoint through a fresh profile; do not reserve a browser port
or attach to an existing browser session.

Use `createBrowserProfile()` before launch and pass that profile to both
`launchBrowser` and `finishFixture`. Capture a primary error and pass it as
`failure` to the finalizer: it attempts every cleanup and reports primary and
secondary errors together. Registered ownership survives a failed launch. Never
remove a profile independently while its process group might still have writers.
The separate production log-stream adapter keeps its deterministic journal input
and owns its PHP workers; it does not read the host journal.

Readiness inherits the enclosing monotonic stage deadline. Every HTTP request,
CDP request, connection, and startup stage is bounded; completed stages cancel
pending child operations. Rendering settles fonts, finite animations, and two
paint frames instead of assuming a fixed delay. An unexpected native browser
dialog fails the test. Expected confirmations require the caller's exact message
allowlist; recovery tests may explicitly decline them.

`node --test tests/browser_harness_test.js` runs real loopback socket/process
fault cases, including fragmented messages, stalled responses, missing replies,
child exit, partial discovery, missing pages, cancellation, occupied PHP ports,
and cleanup denial. Each case has a separate 30-second deadlock watchdog.
Software fixture results do not qualify hardware, installed services, physical
USB, or RF output.
