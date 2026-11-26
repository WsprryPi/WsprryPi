# TODO

Status: ▧ not started · ⏳ in progress · ✅ complete.

Remaining feature items:

1. ▧ Implement I²C band selection for [issue #470](https://github.com/WsprryPi/WsprryPi/issues/470).

   - ▧ Implement Pi-local I²C band selection for [issue #470](https://github.com/WsprryPi/WsprryPi/issues/470). Follow [Prompt 1a for WsprryPi](release_tools/Issue%20470%20I2C%20Band%20Selection%20Implementation%20Prompt.md).
   - ▧ Implement Pico-local I²C band selection for [issue #470](https://github.com/WsprryPi/WsprryPi/issues/470). Follow [Prompt 1b for WsprryPico](release_tools/Issue%20470%20Prompt%201b%20Pico%20I2C%20Band%20Selection%20Implementation.md).

2. ✅ Implement shared device Setup controls and device-owned solar day/night and gray-line scheduling, including remote devices and concomitant Docs updates. Track [WsprryPi issue #473](https://github.com/WsprryPi/WsprryPi/issues/473) and follow the [implementation plan and prompt](docs/development/device-owned-solar-scheduling-plan.md).

3. ✅ Review WTP's remaining role alongside device-owned scheduling and management APIs. Decision: retire WTP while preserving device-owned recurring scheduling, management APIs, and shared execution/safety machinery. Follow item 4 for implementation.

4. ✅ Retire WTP while preserving device-owned recurring scheduling and management APIs.

5. ▧ Keep HTTP/80 as the Pi and Pico dashboard default. Add [optional HTTPS with a local shack CA and automatic LAN renewal](docs/development/browser-hop-security-plan.md). Track [WsprryPi issue #476](https://github.com/WsprryPi/WsprryPi/issues/476). [Implementation prompt](docs/development/browser-hop-security-implementation-prompt.md)

6. ✅ Implement the Fleet Operations device selector, per-device status and controls, required coordinated Pi/Pico changes, documentation, and validation. Follow the [implementation prompt](release_tools/Fleet%20Operations%20Implementation%20Prompt.md).

7. ⏳ Deliver controller-only installation and qualified Linux, macOS, and Windows host support. Track [WsprryPi issue #475](https://github.com/WsprryPi/WsprryPi/issues/475), read the [implementation plan](docs/development/controller-only-linux-plan.md), and follow the [implementation prompt](docs/development/issue-475-implementation-prompt.md).

8. ✅ Implement SQLite persistence and the separate web service. Follow the [implementation prompt](docs/development/sqlite-web-service-implementation-prompt.md) and [storage/process contract](docs/development/sqlite-storage-contract.md). Local software validation is complete; operator-documentation follow-ups and live installation/Pi qualification remain recorded in the contract.

9. ▧ Implement Wsprry-History collection and a product-neutral station observation interface for reception charts and confirmed-completion transmission history. Track [WsprryPi issue #477](https://github.com/WsprryPi/WsprryPi/issues/477) and follow the [implementation plan](docs/development/transmission-history-grafana-plan.md). Keep collection health and advisory calibration evidence separate; Wsprry-History is the sole initial consumer, and other consumers require separate scope and access authorization. Each implementation slice requires its own authorization.

10. ▧ Verify and correct drive-current mappings across WsprryPi and WsprryPico, including direct GPIO, Si5351, RP1, necessary backend changes, and documentation. Track [WsprryPi issue #478](https://github.com/WsprryPi/WsprryPi/issues/478) and follow the remaining [implementation plan](docs/development/legacy-gpio-drive-label-implementation-plan.md).

11. ✅ Allow an admitted 22 m direct CLI Test Tone without a band-selector GPIO. The existing frequency and backend policies still apply. See [WsprryPi issue #482](https://github.com/WsprryPi/WsprryPi/issues/482) and the [Test Tone frequency and selector rules](docs/band-and-wspr-presets.md).
