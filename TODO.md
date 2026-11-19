# TODO

Status: ▧ not started · ⏳ in progress · ✅ complete.

Remaining feature items:

1. ▧ [WsprryPi issue #470](https://github.com/WsprryPi/WsprryPi/issues/470) Follow the [Pi-only I2C band selection implementation prompt](release_tools/Issue%20470%20I2C%20Band%20Selection%20Implementation%20Prompt.md).

2. ✅ Implement shared device Setup controls and device-owned solar day/night and gray-line scheduling, including remote devices and concomitant Docs updates. Track [WsprryPi issue #473](https://github.com/WsprryPi/WsprryPi/issues/473) and follow the [implementation plan and prompt](docs/development/device-owned-solar-scheduling-plan.md).

3. ✅ Review WTP's remaining role alongside device-owned scheduling and management APIs. Decision: retire WTP while preserving device-owned recurring scheduling, management APIs, and shared execution/safety machinery. Follow item 4 for implementation.

4. ✅ Retire WTP while preserving device-owned recurring scheduling and management APIs. Implementation complete; merge CI pending. Follow the [WTP retirement implementation prompt](docs/development/wtp-retirement-implementation-prompt.md).

5. ▧ Review and propose automatic security for the browser-to-device hop, covering web pages, API requests and WebSockets, including pairing-code display and submission. Evaluate HTTPS/WSS, initial browser trust, device address changes and ownership transfer without manual certificate or key provisioning; define Linux/macOS packaging and recovery requirements before implementation.

6. ✅ Implement the Fleet Operations device selector, per-device status and controls, required coordinated Pi/Pico changes, documentation, and validation. Follow the [implementation prompt](release_tools/Fleet%20Operations%20Implementation%20Prompt.md).

7. ▧ After #473, deliver controller-only installation and qualified Linux, macOS, and Windows host support. Track [WsprryPi issue #475](https://github.com/WsprryPi/WsprryPi/issues/475), read the [implementation plan](docs/development/controller-only-linux-plan.md), and follow the [implementation prompt](docs/development/issue-475-implementation-prompt.md).
