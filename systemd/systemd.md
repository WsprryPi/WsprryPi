# WsprryPi Linux services

The installer provisions three WsprryPi process roles from one executable. The checked-in unit templates are authoritative: [backend](generic.service), [browser](browser.service), and [maintenance](maintenance.service). Do not replace them with a single process or grant the browser direct database or device access.

| Service | Operating-system identity | Responsibility |
| --- | --- | --- |
| `wsprrypi.service` | `root`, group `www-data` | Private SQLite state, local transmitter and scheduler, and Fleet/device management |
| `wsprrypi-web.service` | `www-data` | Browser HTTP/WebSocket routes through authenticated local IPC |
| `wsprrypi-maintenance.service` | `root`, group `www-data` | Durable encrypted backup/restore jobs and fixed lifecycle operations |

Apache/PHP remains the UI asset host and reverse proxy. The browser process is a WsprryPi role, not an Apache module. Normal Pi operation retains local output and Fleet together. The existing controller-only role has no local output; it does not imply availability of a dedicated distribution or native service packaging on another platform.

## Backend launch and persistence

The canonical installed backend command is:

```text
/usr/local/bin/wsprrypi -J --state-directory /var/lib/wsprrypi/application-state
```

The installer supplies each unit's exact `ExecStart`. Normal startup opens only admitted state. INI is a one-time migration source; `-i` / `--ini-file` is refused. The backend owns the private selected generation and SQLite/WAL, with IPC under `/run/wsprrypi`. Its directory is group-accessible for socket admission while private persistent state remains inaccessible to the browser account. Keep backend permissions and ownership consistent with the installed templates.

The backend has `Restart=on-failure`, a 30-second restart delay, and a 10-second stop timeout. It is ordered after the independent maintenance service and time synchronization. These service settings do not prove physical output cleanup or establish transmission permission.

## Browser and maintenance lifetime

The browser has a weak `Wants`/`After` dependency on the backend, without stop propagation. It keeps its own secret-free listener cache under `/var/lib/wsprrypi-web`. Its sandbox excludes transmitter devices and `/var/lib/wsprrypi`. Restarting it does not stop or rearm the backend schedule. When the backend is unavailable, ordinary browser operations report unavailability.

The maintenance worker owns `/var/lib/wsprrypi-maintenance` and `/run/wsprrypi-maintenance`, independently of the state generation being replaced. Its fixed service adapter stops and restarts only the backend for an explicitly admitted restore; it is not a general command executor. Durable job status and already admitted recovery downloads remain available across backend replacement. Restored output and remote authority remain held for destination review.

`--no-web` installation omits browser/Apache/PHP setup while retaining backend and maintenance operation. Device network/TLS and USB management remain backend responsibilities.

## Observation and recovery

Read service status and journals separately:

```sh
systemctl status wsprrypi.service wsprrypi-web.service wsprrypi-maintenance.service
journalctl -u wsprrypi.service -u wsprrypi-web.service -u wsprrypi-maintenance.service --since "10 minutes ago"
```

All three WsprryPi units send standard output/error to journald. The validated administrator export `sudo wsprrypi --diagnostic-export` reports supported settings and observations without exposing raw databases or credential stores. Maintenance CLI status can remain useful when the browser is unavailable; settings commands require a running backend. Do not replace a missing established database with defaults, reimport an old INI, or roll back to an incompatible INI writer.

Use the operator manual's [saved settings and local maintenance guide](https://wsprry-pi.readthedocs.io/en/devel/Command_Line_Operations/saved_settings.html) for admitted recovery, including interrupted initial migration. Local fixtures validate software contracts only; installed systemd lifecycle, target timing, GPIO, USB, and RF still require their corresponding runtime evidence.
