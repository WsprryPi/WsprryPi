# Device-owned scheduling

Each Pi or Pico owns its native recurrence and executes locally. Management never dispatches an occurrence to a second transmitter. See [native execution safety](native-execution-safety.md) and [migration](wtp-status-recovery.md).

## Local WSPR message-plan placement

The local Pi scheduler resolves a WSPR USB dial using `C = D + 1500 + r` and compiles each tone as `Fk = C + (k - 1.5) * 1.46484375 Hz`. It selects one uniform random placement in the existing ±80 Hz range when preparing a new message plan. A two-frame plan retains its nominal center and applied placement across both slot requests; the encoded symbol sequences may differ. A subsequent independent plan takes a fresh draw, which may repeat the previous value. Disabled randomization selects zero without calling the provider. Zero is a valid selected placement, distinct from absent plan state.

Placement follows the scheduler's candidate copy, request commit and plan reset boundaries. Re-entering preparation for an active pair reuses its dial, frequency entry and placement, including before its first frame finishes. Forced reconfiguration replaces the plan. A reload deferred during transmission follows the existing completion policy: discard the remaining pair and prepare from the accepted replacement configuration. Invalid or superseded candidates cannot publish placement. Cancellation, failure, skipped entries, completion, mode change, Test Tone restoration and shutdown release the abandoned plan's placement. Backend preparation must succeed before the candidate request and placement are published.

Each committed compiler request still contains one executable WSPR frame. Pair progress and frame identities remain scheduler state. For dial `14095600 Hz` and placement `+37 Hz`, both frames have center `14097137 Hz` and tones `14097134.802734375`, `14097136.267578125`, `14097137.732421875` and `14097139.197265625 Hz`. Placement is nominal RF policy; correction is selected independently for each request, and backend realization or measured RF may differ. Test Tone and CW do not consult this local plan snapshot.

Run the production scheduler and compiler regression without hardware from Linux `src`:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make wspr-plan-placement-test SUDO=
```

The target is also part of full and portable semantics coverage. It uses controlled terminal callbacks rather than waiting through WSPR slots. `ScopedWsprRandomPlacementForTest` is a transient typed developer seam; installation, provider calls and restoration use the scheduler configuration mutex. Providers must not re-enter scheduler configuration. Exception unwinding restores the previous provider. Override lifetimes serialize across threads and nest on the owning thread so an expired callback cannot be restored. No CLI, environment, or persisted setting selects it.

The configuration interoperability target compares test-exported native compiler events with an actual Pico standalone scheduler plan through typed internal fixture calls. This is a software frequency-grid comparison, not a replacement remote job API. It checks paired placement, independent subsequent plans, the native standalone grid, immutable settings, and calibration isolation.

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make -j4 wtp-config-interop-test SUDO= \
  PICO_SOURCE=/path/to/selected/WsprryPico MBEDTLS_SOURCE=/path/to/pinned/mbedtls
```

The recipe builds a separate `wspr_scheduler_frequency_export` test binary with explicit `BACKENDS=simulated ANCILLARY_GPIO=0`. It does not require physical-backend build dependencies or replace the ordinary placement-test binary. The selected Pico sources may include edits; the separate MbedTLS dependency policy still applies. No physical transport, RF, installation, or service qualification is established.
