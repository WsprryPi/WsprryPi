# Wsprry Pi UI

This is the Bootstrap UI for [Wsprry Pi](https://github.com/WsprryPi/WsprryPi/).
It is installed with the current application installation.

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

### Fleet staging flag

The Setup > Fleet tab groups known and nearby WTP devices, connection and status
controls, member management, and output schedules. It starts hidden and disabled
on every page load.
For a local UI review, set `window.WtpUi.developmentControlsVisible = true` in
the browser console; the flag lasts only for that page session. Turning it off
while Fleet is active returns to Transmitter. A saved Fleet URL cannot reveal
it while the flag is off. Showing the tab does not apply endpoint settings or
enable transmission. Profile application, member saves and schedule changes
retain their explicit controls.

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

The PHP fixture router supplies a synthetic, read-only `GET /api/v1/status`
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
