# Browser and production-client validation

The [configuration contract](wtp-configuration-compatibility.md) and
[browser API](wtp-browser-api.md) define member management, compatible atomic
saves, independent station progress and recovery. The selected interoperability
fixtures preserve old and current Pico source pairs with their own expectations.

## Hardware-free coverage

The [Debian workflow](../.github/workflows/debian-non-hardware.yml) runs the
registered software suites. Existing browser regressions under
`WsprryPi-UI/tests` exercise actual request production, draft preservation,
revision invalidation, protected Pi routing, ownership and explicit recovery.
Native fixtures use the real client and selected Pico handlers with temporary
storage, software clocks and inhibited engines. They do not open physical
transmitters or establish RF qualification.

Inspect the current workflow and test entry point before execution. Run tests
against the intended source and retain their actual exit status. A local pass
does not establish that CI passed, and an inhibited endpoint does not establish
physical output behavior.

## Physical and release boundaries

Physical tests require explicit setup authorization, finite operation, exact
source/device/boot/engine/clock identities, and verified restoration. Cancellation
and cleanup receipts describe observed software authority; they do not replace
independent output observation. Installed services, USB timing, RF accuracy,
spectrum and general availability each require corresponding evidence.

The active [Beta 1 evidence](../release_tools/4.0-Beta-1-Release-Evidence.md)
binds the selected candidate, reused acceptance scope and remaining gates.
Cleanup or a later documentation commit does not change that candidate identity
or establish installation/runtime acceptance.
