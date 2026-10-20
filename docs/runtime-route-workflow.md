# Runtime route-manager application integration

## Transient local development confirmation

The canonical managed launch can include one
`--rp1-development-confirmation-json <json>` argument, with optional `--no-web`.
The JSON must contain exactly `enabled`, `route`, `operation_id`,
`physical_connection_confirmed`, `attenuation_and_load_confirmed`,
`bounded_operation_confirmed`, `non_radiating_topology_confirmed` and
`experimental_status_acknowledged`. All boolean fields must be true, the route
must be `GPIO4` or `GPIO20`, and the operation ID must contain 8–64 ASCII letters,
digits, dots, underscores or hyphens. The application also checks the selected
route and current provider state before binding each finite request.

This transient launch argument is available to local WSPR, QRSS, FSKCW and DFCW
scheduling. It is excluded from INI and public configuration serialization.
Keep local Enable off during restart, then enable only the intended bounded
development batch and disable it when that batch ends. The confirmation does
not impose a batch timeout or enable transmission by itself; it binds finite
requests prepared during that process launch. It does not grant incoming WTP
jobs permission or create confirmations on Fleet targets.

Managed preparation may reconcile an inhibited route only with this supplied
confirmation and effective local ownership. Startup/reload inhibition still
blocks preparation. The resolved route, provider and ordinary transmit gates
remain mandatory before request commitment. The fixed service validator rejects
alternate binary/configuration paths and unsupported CLI overrides.

After changing a service launch argument, restart while local Enable is off
and verify the running process received it. A refreshed systemd unit definition
alone does not show the arguments of an already running process. When updating
the installed route companion, use the provider's supported neutral update and
digest-bound redeployment workflow; its binding includes that companion's hash.
Keep Avahi client development headers present for native DNS-SD builds and
verify both advertisement and discovery after deployment.

## Route and startup lifecycle

This companion implementation consumes the explicit
`rp1-gpclk-route-manager-runtime` profile from RP1-GPCLK-DKMS. The
packaged/source-development protocol remains separate. Unknown contracts fail
closed; discovery of the runtime profile asserts the transmission inhibit. Explicit
`idle` and `reconcile-output` requests establish runtime route readiness for
startup and the existing operation-scoped development authorization path. Neither
query authorizes output.

The installer stops in `neutral_ready` with no route. The existing route panel
is the later explicit operator choice: preflight calls upstream `route-plan`,
reviews and retains its SHA-256 digest, and **Switch route (stay idle)**
queues `route-ensure` in a bounded transient system service only for that same
route and preflight generation. This keeps the HTTP request out of the service
stop/restart transaction and prevents the application from waiting on its own
shutdown. A queued response confirms submission, not route completion. The
application requires its full idle predicate and keeps requested state separate
from controller-reported active state.
Recovery explicitly asks the controller to reach no route; it is not a rollback
to a previous GPIO. Removal errors retain their kernel errno and overlay ID.

The manager temporarily inhibits systemd starts using its own service drop-in,
stops WsprryPi, and switches the owned runtime overlay while the application is
idle and transmission is not authorized.
It does not overwrite the installed service unit or an administrator's mask.
After the route is verified idle, the installed WsprryPi companion updates the
backend to `rp1-gpclk`, updates the saved pin, and disables transmission. Before
this explicit selection, a stock `gpio` backend is accepted for neutral
inspection but is left byte-for-byte unchanged. A previously running application restarts
in idle mode; a previously stopped or administrator-masked application stays
stopped. Saved `Enable on Boot` preferences and unrelated configuration remain
unchanged. A stopped application's first subsequent startup is also idle.

On a clean reboot, the application-owned `wsprrypi-rp1-reconcile.service`
obtains fresh public provider activation and route plans. It runs outside the
application service because neutral activation and route restoration stop and
restart WsprryPi. A complete terminal prior-boot selection must match the saved
GPIO4 or GPIO20 route. Neutral state or completed removal does not implicitly
select a route. Ambiguous history, changed identities, a changed saved route,
an intentionally stopped application, or an administrator mask fails closed.

The startup pre-hook reads attributable history without querying the manager
socket. If provider state is temporarily absent during installation or is
unproven, the application can still start idle for diagnostics; the worker
refuses administration until ownership is established. `WSPRRYPI_RP1_REBOOT_IDLE` keeps bootstrap startup idle and skips an
obsolete route-readiness acknowledgement. It is distinct from the manager's
fresh restoration token. Once the new route is restored, normal startup must
acknowledge that exact token. Both flags keep transmission disabled without
changing the saved boot preference. A checkpoint under
`/var/lib/wsprrypi/rp1-runtime-reconcile.json` binds the current boot, provider
binding, activation-plan digest and saved route across worker interruption.
Unsupported or interrupted provider hardware transitions retain their evidence
for the provider's explicit recovery path.

The browser may disconnect when its application stops. The Setup route-status
dialog remains open, treats this interruption as expected, and retries read-only
status queries with bounded backoff after the application restarts. It never
repeats the route mutation. If automatic checks pause, the operator may retry a
status query or close the dialog; `runtime_route_client.py query` reports the
durable transaction when the application does not return. Application
readiness is acknowledged only after startup reconciliation, loop setup and
binding the HTTP listener when enabled; a
systemd start request alone is not success. The UI distinguishes restoration in
progress, successful completion, route recovery, and application restoration
failure. Successful completion is labeled **Route selected** and names the
active GPIO while explicitly retaining idle mode. Runtime administration does
not display development-provider fields that its response cannot substantiate;
those absent fields are not evidence that the selected route failed. Completed
records describe the last transaction, not future uptime or transmission
authorization.

`runtime_route_client.py restore --execute` retries incomplete application
restoration on the installed route without repeating overlay removal/application.
An interrupted route change requires `recover --execute`, followed by a new
explicit switch. Prior-boot ownership and transmission intent are never reused.
Normal clean-reboot reconciliation uses the separately described startup worker.
Transmission is not resumed by either switching or restoration. The normal
operator controls and application-owned RP1 operation authorization remain
available; the kernel endpoint does not receive an authorization credential.
On a runtime-profile system, choosing **None** for **Transmit Pin** exposes the
existing explicit recovery-to-neutral operation as **Remove route**. The UI
passes the last confirmed GPIO route to recovery, does not persist an invalid
pin, and leaves transmission disabled. The detailed route fact panel remains
in the page source for debugging but is hidden from the normal operator layout;
the action and concise state pill sit beside the selector.
An application update after same-boot route recovery may retire the neutral
controller only through the provider's digest-bound activation-recovery plan;
the completed neutral activation and recovered route journal chain must both
remain exact.

The application installer and `scripts/copy_exe.py` install
`/usr/local/lib/wsprrypi/route_application.py` with the executable. Use a matching,
newly bound DKMS runtime bundle reporting `applicationRestoration: true`.
The companion supports the canonical `/usr/local/bin/wsprrypi -J -i
/usr/local/etc/wsprrypi.ini` service command, optionally with `--no-web`.
Route-neutral inspection accepts either the GPIO family or Si5351 as the saved
transmit backend without rewriting it. A later explicit GPIO route selection
atomically changes the backend to `rp1-gpclk`, applies the selected pin, and
forces transmission off before the service can start.
It refuses missing services, unsupported command overrides, alternate config
paths or old binaries before stopping the application. Administrator-owned
units using that command are preserved. An old service mask from a previous
manual deployment must be reviewed during installation; it is not automatically
removed or assumed to belong to this workflow.

The startup-only `WSPRRYPI_ROUTE_RESTORE_IDLE` environment value is supplied by
the manager's owned drop-in for a current route transaction. It is not a transmission permit or operator setting.
Startup forces `Transmit=false` while preserving the stored boot preference;
the token binds the application's readiness acknowledgement to this transaction.

Source-level validation does not qualify target deployment, route switching,
service restart, GPIO, or RF behavior. Those operations require separate
authorization and target-specific validation.

Bounded-tone start acknowledgements are asynchronous. A response includes an
RP1 operation record only when it belongs to that request; the terminal record
retains provider failure reasons and cleanup failures. A worker exception is
reported without terminating the application process.
