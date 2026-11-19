# Pico network management

External WTP execution is retired. There is no `backend=wtp`, Pi WTP listener, remote Fleet dispatch, Pico WTP TCP/TLS/USB/BLE carrier, or generic HTTP jobs API. No hidden downgrade or alternate job-submission path is supported. Existing WTP clients and Qualification Harness WTP campaigns are incompatible with this software; their adapters require separate work and are not replaced here.

Each device executes its own native schedule. See [native execution and output safety](native-execution-safety.md), [migration and recovery](wtp-status-recovery.md), and [Pico USB management](wtp-usb-management.md). Do not replace WTP-capable software until its deployment’s legacy authorities and cleanup duties are resolved.

Certificate-authenticated engineering HTTPS retains TLS certificate, hostname/IP identity, mutual authentication, qualified certificate time, and `http/1.1` ALPN. `wtp/1` is refused. Consumer-Radio retains its separately authenticated encrypted HTTP management and commissioning; no WTP key derivation or authentication fallback is introduced.

DNS-SD management discovery uses `_http._tcp` with the exact current management TXT descriptor. An observation is a candidate locator, not authentication. Preserve explicit saved hostnames, ports, identities, and credentials. Never replace an explicit port with a default or fall back to an older carrier. New manual Consumer-Radio profiles default to port 80.

The actual network acceptance recipes remain `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test` in the [Debian workflow](../.github/workflows/debian-non-hardware.yml). Their historical names do not advertise WTP support. Select the edited Pico source and pinned MbedTLS explicitly. Both network and USB actual-source suites are required for a shared control change.

## Tool compatibility

The maintained Pico `rf_wtp.py`, `wtp_probe.py`, `wtp_monitor.py`, `usb_inventory.py`, `check_usb_target.py`, and `inhibited_network_acceptance.py` command entry points refuse before device I/O. Console-only trace and standalone diagnostics use the independent Console helper. Independent decoding, calibration, RFBench Commands, and bounded transport test libraries retain their own responsibilities.

The sibling Qualification Harness’s WTP-based live Pico smoke and execution campaigns depend on its `wtp_control`, `pico_live`, and `pico_smoke` adapters and cannot operate this candidate. Those sources were inspected read-only; no harness compatibility or replacement remote API is provided. Keep its independent capture/decoder workflows separate from retired transmitter control.
