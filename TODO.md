# TODO

Status: ▧ not started · ⏳ in progress · ✅ complete.

Remaining feature items:

- ▧ [WsprryPi issue #470](https://github.com/WsprryPi/WsprryPi/issues/470)
  Follow the [Pi-only I2C band selection implementation prompt](release_tools/Issue%20470%20I2C%20Band%20Selection%20Implementation%20Prompt.md).
- ▧ Implement shared device Setup controls and device-owned solar day/night and
  gray-line scheduling, including remote devices and concomitant Docs updates.
  Track [WsprryPi issue #473](https://github.com/WsprryPi/WsprryPi/issues/473) and
  follow the [implementation plan and prompt](docs/development/device-owned-solar-scheduling-plan.md).
- ▧ Review WTP's remaining role alongside device-owned scheduling and management APIs: identify capabilities still needed by remote jobs, the WTP backend and existing clients; decide whether to retain, consolidate or retire those paths, and evaluate automatic pairing-based TLS for retained WTP communications using separate purpose-bound keys. Existing WTP TLS support and compatibility remain unchanged until that review is approved.
- ▧ Review and propose automatic security for the browser-to-device hop, covering web pages, API requests and WebSockets, including pairing-code display and submission. Evaluate HTTPS/WSS, initial browser trust, device address changes and ownership transfer without manual certificate or key provisioning; define Linux/macOS packaging and recovery requirements before implementation.
- ✅ Implement the Fleet Operations device selector, per-device status and
  controls, required coordinated Pi/Pico changes, documentation, and validation.
  Follow the [implementation prompt](release_tools/Fleet%20Operations%20Implementation%20Prompt.md).
