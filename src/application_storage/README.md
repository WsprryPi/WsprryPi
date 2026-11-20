# Application storage

This directory contains backend-owned SQLite storage and private adapters for WsprryPi settings, catalog, native receipts, recurrence authority, and initialization. It is part of the application and is not a shared settings/control protocol or a database administration interface. The database does not queue commands.

`SqliteStateStore` provides bounded records, transactional revision checks, private POSIX ownership checks, immutable first-decision provenance, and consistent SQLite snapshots. It does not validate settings semantics, grant output authority, or adopt external edits. The existing catalog and native managed-state owners retain those responsibilities through their adapters.

`PersistentConfiguration` separates saved configuration and private policy from transient runtime observations and iteration counters. `ApplicationStateSelection` selects one namespace for the backend lifetime and refuses file fallback after selection or teardown. Browser and CLI processes must use the backend interface rather than selecting this store.

`state_migration` admits an initial generation under a separate root lock. The native-core migration path retains original source backups and supports interrupted retirement without reimporting an active INI. The fresh path publishes only deterministic initial records and keeps operator setup separate from native activation. The executable now selects established SQLite state and dispatches explicit initialization/migration before hardware metadata, with actual-main fixture coverage. Companion capture validates existing Fleet/logging/journal owners and streams files into the admitted generation. Installer and route-maintenance integration have focused software coverage. Advanced local settings use authenticated backend IPC with expected revisions. The separate HTTP/WebSocket service uses authenticated, bounded local IPC and cannot open this state. The shared encrypted recovery engine, independent maintenance worker, persistent restore holds, and allowlisted diagnostic export use these owners; see the [storage and process contract](../../docs/development/sqlite-storage-contract.md).

Focused hardware-free checks run from `src` with `BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=`:

```sh
make sqlite-state-store-test persistent-configuration-test sqlite-catalog-test state-migration-test database-configuration-runtime-test database-managed-startup-test database-fresh-startup-test controller-settings-test database-controller-runtime-test controller-retirement-storage-binding-test BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
```

These checks use private temporary fixtures. They do not access Pis, installed application state, services, GPIO, or RF. POSIX software evidence does not qualify Windows ownership adapters, installation, Pi timing, or hardware operation.
