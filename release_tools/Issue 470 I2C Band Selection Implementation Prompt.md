# Issue 470 I2C Band Selection Implementation Prompt

Implement WsprryPi issue [#470](https://github.com/WsprryPi/WsprryPi/issues/470): optional Pi-local I²C band selection using 8574-family expanders, with Simple and Advanced/Binary modes.

**This is a Pi-only task. Do not modify WsprryPico.** Pico-connected expanders are separate work. Reading an unchanged Pico checkout and using it in hardware-free interoperability fixtures is permitted; changing its source, configuration, documentation, firmware, or devices is not.

## 1. Scope and authority

Implement the feature in `/Users/lbussy/GitHub/WsprryPi`, including its first-party `WsprryPi-UI` component, appropriate tests, and engineering documentation.

Update affected Pi operator documentation in `/Users/lbussy/GitHub/WsprryPi-Docs`. Verify the actual repository path and read its instructions before editing; do not create a replacement repository because an older instruction names a different path.

Before implementation:

- Read applicable `AGENTS.md` files.
- Inspect branches, complete working-tree status, staged and unstaged changes, and affected component boundaries.
- Preserve all existing user changes.
- Read issue 470 and its developer implementation comment. This prompt overrides the comment wherever necessary to enforce the Pi-only boundary.
- Trace configuration, band selection, amplifier/PTT, scheduling, cancellation, shutdown, I²C ownership, and the separate Pi WTP endpoint execution path.
- Use Impeccable for UI design, implementation, and visual review.

No staging, commits, pushes, issue replies, pull requests, publication, installations, service operations, system configuration changes, or live hardware/RF operations are authorized.

Do not implement Fleet Operations changes, Pico expander support, native multi-bit GPIO encoding, unrelated expander protocols, SWR/power telemetry, or amplifier circuitry.

## 2. Supported hardware and modes

Support only verified-compatible 8574-family eight-bit expanders, in both forms:

- Logic-output breakout, using the [Adafruit PCF8574 breakout, product 5545](https://www.adafruit.com/product/5545), as the documented example.
- Eight-channel relay board, using [KRIDA Electronics Amazon ASIN B083LFPCK8](https://www.amazon.com/dp/B083LFPCK8), as the candidate example. Its actual revision, chip, channel mapping, polarity, and electrical connections require verification before board-specific qualification claims.

Use one full-port-byte driver and shared pattern generation for both forms. Do not require mechanical relays to obtain binary logic outputs.

Provide:

| Mode | Behavior |
|---|---|
| Simple | Each band selects one of eight logical outputs or is explicitly unassigned. All other outputs are logically inactive. Multiple bands may share an output. |
| Advanced/Binary | Each band has an arbitrary eight-bit logical pattern from 0 through 255, inclusive. Any combination is valid, including zero and all eight outputs. |

`0x00` is a configured pattern, not an unassigned marker. Binary mode is not restricted to BCD.

P0 is bit 0; P7 is bit 7. Display bit order as **P7 … P0** and provide the corresponding numeric value.

Preserve independent Simple and Binary assignments when switching modes. Do not convert or overwrite one assignment set merely because the operator explores the other mode.

## 3. Configuration and activation contract

Keep I²C selection disabled by default and preserve existing valid native Band GPIO and frequency-suffix behavior.

Make routing explicit: disabled/manual, existing native GPIO, or the new I²C selector. Preserve existing installations’ effective routing when the new configuration is absent. Permit only one active automatic band-selection route; do not silently activate native and I²C selectors together.

An explicit native GPIO frequency suffix must not override an active I²C route or create a second selector. Reject conflicting active requests with an actionable explanation.

Define a typed configuration containing:

- Route and Simple/Binary mode.
- Supported chip variant and board/channel mapping.
- Explicit adapter selection using the existing durable Linux identity policy.
- Seven-bit device address.
- Independent Simple and Binary per-band assignments.
- Per-output inversion mask, with all-normal/all-inverted conveniences.
- Explicit startup/shutdown idle pattern.
- Between-job behavior: return to idle or hold the last successfully selected pattern.
- Release and settling intervals in whole milliseconds.
- An optional intermediate pattern and its timing only where explicitly configured and meaningful for the attached circuit.

Define logical 1 as an asserted output. For identity mapping:

`physical_byte = logical_pattern XOR inversion_mask`

If a verified board uses a permutation, define the inversion mask in logical-output order and apply the documented permutation afterward. Apply inversion and mapping exactly once for target, intermediate, idle, and cleanup writes.

Do not assume logical zero disconnects a filter or represents a safe receiver code. Make the operator’s idle selection explicit. Preserve all eight pattern bits; do not allocate one to PTT.

New selector-domain changes must:

- Be validated and committed atomically while idle and unowned.
- Preserve an immutable active configuration for the running process.
- Require an explicit application restart to activate.
- Latch transmission inhibition until restart, including an A/B/A sequence of saves.
- Never test or switch the newly configured outputs during save.
- Leave unrelated native configuration activation behavior unchanged.

Do not restart a service automatically.

Trace and update defaults, parsing, validation, internal copies, persistence, serialization, UI population, and runtime consumption together. Reject malformed types, unknown fields in the new closed configuration object, invalid ranges, unsupported profiles, and conflicting active allocations before persistence or selector writes.

Choose and document finite timing bounds based on scheduler preparation budgets. Do not treat a convenient default as a measured relay requirement. If a material timing or hardware decision cannot be supported by the available evidence, report that specific blocker instead of inventing compatibility.

## 4. Driver and I²C ownership

Separate pure pattern resolution, selector lifecycle, and physical transport. Keep the selector independent of the RF-generation backend.

Use a single owner for the selector state and writes. Maintain:

- The intended logical pattern.
- The last successfully written full port byte.
- Whether the commanded state is currently known.
- Fault and initialization state.

Write the complete eight-bit port value in one supported transaction. Do not perform per-bit read/modify/write transactions, reconstruct output latches from input reads, or add register-based direction initialization.

Explicitly support PCF8574 addresses `0x20–0x27` and PCF8574A addresses `0x38–0x3F`. Any additional 8574-family variant requires verified transaction, electrical, and address compatibility. Reject XL9535 and other unsupported families.

Follow the current Linux adapter-identity contract:

- Use metadata-only discovery.
- Preserve durable USB identity and detach/rebind refusal.
- Never substitute another adapter after resolution fails.
- Never probe addresses or issue discovery writes to identify hardware.
- Never use forced access to bypass kernel ownership.
- Reject known address conflicts and serialize cooperating process access.

Do not claim that advisory locking excludes arbitrary noncooperating programs using `i2c-dev`.

Account for coexistence with Si5351 on the same bus: distinct addresses, compatible bus speed, transfer serialization where necessary, and no selector activity during RF-sensitive execution. Do not silently change system bus configuration. Use a speed supported by every attached device; PCF8574 requires no more than 100 kHz.

Prefer existing reusable identity facilities without unrelated component changes. If a necessary change reaches `src/WSPR-Transmitter`, identify the exact interface need, explain why parent-level integration is insufficient, and obtain any approval required by repository instructions before broadening component scope.

## 5. Fail-closed sequencing and execution coverage

Do not add an I²C write to the existing transmit-start notification and assume that creates a safety gate. The current Pi callback path can log selector failures and continue to amplifier activation.

Implement a preparation path whose failure prevents RF admission:

**RF disabled → amplifier/PTT inactive → release interval → optional intermediate pattern → complete target-byte write → settling interval → amplifier lead → scheduled RF.**

Preserve the existing amplifier timing contract. Release and settling are separate intervals, not aliases for amplifier lead or tail.

At startup, establish RF inhibition and PTT inactivity, initialize the selected expander to its configured idle pattern, and permit dependent transmission only after initialization succeeds.

Resolve selection from the actual job’s transmitted frequencies, including custom frequencies, WSPR offsets, modulation shifts, and applicable correction. Do not use an unrelated browser selection or a WSPR dial frequency as the RF frequency.

Freeze the resolved pattern and configuration for the accepted job. Every RF-on event must be compatible with one selection for the complete job. Reject jobs requiring a pattern change during uninterrupted execution.

When I²C selection is active, unassigned or unresolvable frequencies must refuse transmission. They must not reuse the preceding band’s pattern.

Complete preparation before the scheduled RF start. Include its cost in admission and refuse insufficient lead time; do not move the requested RF window or switch filters after RF has started.

Cover every relevant Pi RF entry point:

- WSPR and other supported local modes.
- Direct and managed test-tone paths.
- Repeated scheduling and configuration reloads.
- Jobs received by the Pi’s own WTP endpoint.
- Cancellation, STOP, shutdown, and error cleanup.

The Pi WTP endpoint must use the same owning selector state and preparation gate, rather than bypassing it through its separate engine.

A Pi acting as a controller must not operate its local expander for jobs targeting another Pi or Pico. Unsupported remote-selector combinations must be refused explicitly.

During a job, retain selection through keyed gaps and normal amplifier tail. At normal completion, stop RF before releasing PTT, then apply the configured between-job behavior.

On cancellation or fault:

- Inhibit further RF and attempt independent PTT release.
- Attempt configured idle cleanup without concealing earlier failures.
- Mark uncertain selector state unknown.
- Bound retries and invalidate stale pending work.
- Prevent delayed writes from reasserting outputs after cancellation or shutdown.
- Require successful reinitialization before reuse.

For this implementation, recovery may require explicit application restart; do not add an automatic retry loop that resumes transmission after an uncertain write.

A failed cleanup may leave an externally powered expander latched. Preserve that limitation in status and documentation.

## 6. Pi/Pico contract boundary

Do not change WsprryPico source, firmware, documentation, settings, schemas, generated assets, or devices.

Do not advertise Pi-local expander support as a Pico capability. Preserve the existing Pico selector editor and behavior.

Keep WTP wire formats, shared Control/1 schemas and descriptors, and independent transport/storage versions unchanged. Do not add a new shared-control version, downgrade path, alternate carrier fallback, or relaxed parser.

Implement through Pi-local configuration and execution wherever possible. If safe implementation requires a shared contract change or a Pico modification:

- Do not make that change.
- Explain the exact dependency and affected consumers.
- Report the remaining implementation as blocked at that boundary.
- Do not substitute incomplete validation or claim the issue complete.

An unchanged Pico checkout may be used as the companion in production-path integration fixtures. Record its exact revision.

## 7. User interface

Extend the existing Pi I/O configuration workflow using Impeccable and the established Bootstrap layout, typography, themes, controls, and validation patterns.

Present routing first, then I²C hardware details and per-band assignments.

- Simple mode: a channel selector and explicit unassigned choice.
- Advanced/Binary mode: eight labeled logical bits and synchronized numeric representation.
- Common controls: polarity, idle behavior, release, and settling.
- Clearly label physical channel numbering where relay channels 1–8 correspond to P0–P7.

Keep advanced details in inline disclosures rather than introducing a separate application or dashboard.

Use explicit save for the complete selector configuration. Mode exploration and preview must remain local drafts and must not trigger the existing generic autosave path.

Preserve drafts after validation errors, unavailable hardware, and uncertain saves. Show restart-required and transmission-inhibited states beside the relevant controls.

Distinguish:

- Saved settings.
- Active settings.
- Intended pattern.
- Last successful byte write.
- Unknown or stale state.

Do not label an I²C ACK or port read as verified relay position, verified filtering, or RF safety.

Do not add an interactive live-output test button.

Render and inspect desktop/mobile layouts in light and dark themes, including keyboard navigation, focus, field errors, disabled controls, long adapter identities, restart-required state, and hardware-fault feedback.

## 8. Documentation and electrical boundaries

Update Pi operator documentation and relevant Pi engineering documentation alongside implementation. At minimum, review:

- `WsprryPi-Docs/docs/User_Interface/Setup/Pi_IO/index.md`
- Relevant transmitter and Linux I²C setup instructions.
- `WsprryPi/docs/development/linux-i2c-identity.md`
- Relevant Pi scheduling, configuration, and WTP endpoint documentation.

Add separate wiring guidance for the logic breakout and verified relay-board form. Explain:

- Pi-facing SDA/SCL and pull-ups must remain at compatible 3.3 V levels.
- A relay board’s supply voltage does not establish I²C compatibility.
- Required level translation, grounding/isolation, relay power, and backfeed prevention depend on the actual board.
- PCF8574 outputs are quasi-bidirectional, with weak-high and strong-low behavior.
- Logic outputs are not relay-coil drivers, dry contacts, or RF amplifiers.
- External buffering and receiver thresholds require verification.
- I²C should remain local unless a separately qualified physical arrangement supports otherwise.

PCF8574 powers up high. Software cannot guarantee a safe pattern before initialization, after a crash, or during power loss. Require an independently safe amplifier/PTT or hardware-enable arrangement for the connected system. A complete-byte write does not guarantee simultaneous relay movement or glitch-free external decoding.

Use the actual chip datasheet and board documentation:

- [PCF8574 datasheet](https://www.ti.com/lit/ds/symlink/pcf8574.pdf)
- [PCF8574A datasheet](https://www.ti.com/lit/ds/symlink/pcf8574a.pdf)
- [Adafruit wiring guide](https://learn.adafruit.com/adafruit-pcf8574?view=all)

The issue’s original band table and developer example disagree about code 1 being 60 m or 160 m. Do not ship an authoritative G59 preset without resolving that discrepancy. Configurable mappings and explicitly labeled test fixtures can proceed without a preset.

Build operator documentation using its repository’s virtual environment and documented Sphinx workflow. Inspect affected rendered pages and replace screenshots only when materially inaccurate. Do not modify Pico documentation.

## 9. Software validation

Read `docs/simulated-backend.md` before application-level testing. Use explicit simulated RF execution and an explicit fake selector transport.

Neither selecting the simulated backend nor running ordinary tests may open physical I²C hardware, even when a saved configuration enables the selector. Do not rely solely on missing hardware or permissions to make tests safe.

Add meaningful tests covering:

- Both board forms and both modes.
- All eight output positions.
- Shared-band assignments.
- `0x00`, `0xFF`, and multi-output patterns such as `0x05`.
- Per-output inversion, verified permutations, and exactly-once conversion.
- Separate preserved mode assignments and persistence round trips.
- Missing assignments versus valid zero.
- Strict refusal before selector writes.
- Existing native GPIO and frequency-suffix regressions.
- Both address variants and unsupported-profile rejection.
- Exact full-byte transactions, failed/short writes, adapter loss, and uncertain state.
- Startup idle, release/settling ordering, insufficient lead, normal completion, hold/idle behavior, cancellation, configuration changes, and shutdown.
- No stale writes or RF restart after failure.
- Local and Pi endpoint execution, including isolation from jobs targeting remote devices.

Use observable fake I²C/PTT/RF events and deterministic clocks where possible. Verify actual production orchestration, not merely a second model of the intended sequence.

Run applicable existing targets from `src`, after inspecting their recipes and selecting a supported host profile:

```sh
make band-gpio-default-convergence-test band-gpio-rotation-test \
  selector-shutdown-cleanup-test test-tone-selector-plan-test \
  linux-i2c-identity-test SUDO=
```

On macOS, run:

```sh
make semantics-test-portable SUDO=
```

On the appropriate Linux host, run:

```sh
WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 make semantics-test SUDO=
```

Portable success is not a substitute for the full Linux profile.

Run affected standalone component tests if component source changes are authorized. In `WsprryPi-UI`, extend and run:

```sh
npm run test:unit
npm run test:browser
```

Add checks that configuration exploration and saves cannot issue selector writes or silently activate a different route. Include the repository’s hardware-access audit for application simulations.

## 10. Production interoperability and physical qualification

Because this work integrates with Pi WTP admission and execution, run both checked-in hardware-free source-pair recipes in `.github/workflows/debian-non-hardware.yml`, even though Pico remains unchanged:

- `wtp-network-loopback`: include `wtp-network-runtime-test`, `wtp-network-interop-test`, and `wtp-config-interop-test`.
- `wtp-usb-management-pair`: build the actual unchanged Pico `usb_management_driver`, run `wtp-usb-pair-client-build` and `wtp-usb-management-test`, and execute `WsprryPi-UI/tests/wtp_usb_pair_test.js` through the production pair.

Select and record the exact Pico source and pinned dependencies explicitly. Do not silently switch to an older companion, weaken validation, or edit Pico to obtain passing results.

Add feature-specific Pi endpoint coverage using simulated RF and the fake selector. Existing interoperability success alone does not prove the new sequencing.

Failed, timed-out, cancelled, or skipped required checks remain unresolved. If an unchanged companion prevents validation, report the blocker without modifying it.

Physical testing is not authorized by this prompt. Record a separate qualification procedure for later approval, covering both actual board forms with RF initially disabled:

- Power-up and power-loss states.
- Chip identity, mapping, polarity, pull-up rails, and electrical compatibility.
- Release, settling, and PTT ordering.
- Bus disconnection and failed cleanup.
- External enable/interlock behavior.

Any later RF test requires explicit authorization and exact frequency, mode, duration, output path, attached hardware, and stopping procedure. Software tests do not qualify relay contacts, filter response, RF output, or physical timing.

## 11. Adversarial review and completion

Review the implementation, tests, and documentation adversarially before declaring completion. Specifically challenge:

- RF paths that bypass selector admission.
- Errors that are logged but do not prevent transmission.
- Zero patterns mistaken for missing assignments.
- Polarity applied twice or in inconsistent coordinate systems.
- Native/I²C routing conflicts.
- Mode changes that overwrite assignments.
- Saves or previews that switch hardware.
- Late writes after cancellation.
- Unknown state treated as successful cleanup.
- Simulated runs reaching physical transports.
- Pi-local hardware following remote-device jobs.
- Pico or shared-contract scope expansion.
- Unsupported electrical or qualification claims.

Fix actionable findings, rerun affected checks, and repeat the review until no actionable findings remain. If a finding cannot be resolved within scope or available evidence, report it as an explicit blocker; do not call the assessment clean.

The completion report must state:

- Implemented behavior and important files/components changed.
- Exact Pi and unchanged Pico source revisions, including tested uncommitted changes.
- Commands run and their pass/fail/skipped results.
- UI workflows and desktop/mobile visual evidence reviewed.
- Documentation updated, considered unchanged, and still required.
- Remaining hardware qualification and other unresolved validation.
- Final branches and working-tree state.
- Confirmation that Pico was not modified.
- Confirmation that no commit, push, installation, service operation, or live hardware/RF action occurred.

Distinguish software implementation from physical qualification. If required work remains incomplete, identify the exact next unfinished step.
