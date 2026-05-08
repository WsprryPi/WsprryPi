# Wsprry Pi v3.0.0-rc.1 Release Changelog

Generated from the commit range:

```text
30b39e2bc9ff4068b9b3532534ebad6392958139..687f77ed8258c022b3c6dd7bf4c036651b54454b
```

## Summary

This release is a major runtime, configuration, UI, and hardware-support update.
The largest changes are selectable RF backends, Si5351 support, stronger WSPR
planning semantics, QRSS/FSKCW/DFCW runtime improvements, a reorganized web UI,
more deterministic GPIO handling, and Raspberry Pi 5 installation/build support.

## Highlights

- Added selectable transmit backend support for GPIO and Si5351.
- Added Si5351 runtime configuration, CLI options, INI support, and web UI
  setup controls.
- Reworked the web UI into a dedicated Operation landing page plus a separate
  Setup area.
- Added stronger runtime state handling for Stop, transmit enable, websocket
  updates, and headless operation.
- Hardened WSPR paired-frame planning.
- Improved QRSS/FSKCW/DFCW handling, including CW gap controls, fade settings,
  message normalization, and active-character progress reporting.
- Reworked LPF selector GPIO handling so configured selector lines remain in
  deterministic inactive states between transmissions.
- Added Raspberry Pi 5 installer support while preventing unsupported GPIO
  transmission on Pi 5 and unknown platforms.
- Added a wspr.live direct ClickHouse fallback path with downloader-first auto
  failover.

## Hardware And Platform Support

### Added

- Added selectable GPIO or Si5351 transmit backend support.
- Added Si5351 I2C bus, I2C address, reference frequency, power level, and TX
  output configuration for CLI and INI use.
- Added Si5351 test-tone support through the normal controller/backend execution
  path.
- Added backend-aware help and startup logging so GPIO-only and Si5351-only
  options are shown in the right context.
- Added installer support for Raspberry Pi 5.

### Changed

- GPIO transmission is now restricted to Raspberry Pi 4 and earlier.
- Raspberry Pi 5 and unknown platforms are blocked from GPIO transmission at
  config validation, scheduler, and backend guard points.
- The installer no longer forces an unnecessary reboot when I2C is enabled by
  `raspi-config`.
- GPIO transmit pin selection is constrained to GPCLK0-capable GPIO4 and GPIO20.
- Si5351 unused outputs are held safe for app-managed runs, while unused-output
  parking remains internally planned for future refinement.

### Fixed

- Fixed Pi 5 and Bookworm compilation issues by guarding libgpio-related build
  paths.
- Fixed generic GPIO ambiguity by resolving BCM lines through a shared resolver
  instead of assuming `/dev/gpiochip0` or BCM equals chip offset.
- Collapsed duplicate gpiochip views that expose the same underlying GPIO
  controller.
- Improved GPIO setup and write diagnostics for selector GPIO, TX LED, and
  shutdown button paths.

## Web UI And Operator Experience

### Added

- Added a dedicated Operation page as the default landing view for runtime
  status and transmit control.
- Renamed the previous configuration-focused surface to Setup.
- Added a Transmitter Hardware tab for GPIO and Si5351 settings.
- Added a frequency indicator to the Operation page.
- Added offline/online detection and clearer degraded/backend unavailable
  runtime messages.
- Added shared Bootstrap modal dialogs for transmit failures, reset actions, and
  paired-planning diagnostic details.
- Added local vendored frontend dependencies, including Bootstrap, Bootswatch,
  jQuery, icons, and fonts.

### Changed

- Consolidated UI routing under `index.php` with page-specific partials, CSS,
  JavaScript, titles, and metadata.
- Moved live operation controls away from setup-only controls to reduce
  accidental configuration changes.
- Reworked layout, spacing, responsive behavior, navbar state, footer offset
  handling, logs, spots, and maintenance presentation.
- Made theme handling more consistent and reduced initial theme flash.
- Improved accessibility with better heading structure, toolbar semantics, ARIA
  live updates, larger hit areas, and reduced-motion support.
- Replaced direct `alert()` and `confirm()` flows with the shared modal pattern.
- Hardened DOM updates by replacing fragile `innerHTML`/`.html()` paths with
  safer node creation and text updates where practical.

### Fixed

- Fixed Stop behavior in the web UI by routing Stop over the websocket control
  path and synchronizing button state with runtime status.
- Fixed disappearing CW controls by keeping CW Control in the Radio tab.
- Fixed stale WSPR runtime data leaking into idle CW snapshots.
- Fixed runtime status flow so transmit progress websocket events are treated as
  transmitting.
- Fixed repeated autosave attempts after unchanged failed autosaves.

## Runtime, Scheduling, And Shutdown

### Added

- Added headless startup support with `--no-web`.
- Added more explicit runtime state tones for ready, active, warning, degraded,
  offline, and loading states.
- Added MonitorFile handling for stable delete, recreate, and atomic replace
  detection.

### Changed

- Made real-time TX scheduling best-effort instead of fatal if elevated thread
  scheduling cannot be applied.
- Reordered shutdown so runtime threads are stopped before reboot, poweroff, or
  normal exit.
- Deferred managed INI reloads during active transmission windows so config file
  changes do not cancel active transmissions.
- Improved invalid-reload messages so they clearly state that transmit is blocked
  until a valid configuration is loaded.
- Applied LED, shutdown GPIO, and web-port fields during managed config reloads.

### Fixed

- Fixed active GPIO-backed Stop handling by replacing uninterruptible absolute
  event waits with stop-aware waits.
- Fixed signal handling race and restored graceful shutdown under systemd.
- Fixed reload behavior for Use LED, LED Pin, Use Shutdown, Shutdown Button, and
  related live config state.

## WSPR Planning And Transmission Semantics

### Added

- Added planner preference support with `auto`, `prefer_paired`, and
  `require_paired` modes.
- Added runtime/WebSocket reporting for WSPR plan and frame metadata.
- Added paired-transmission progress reporting through scheduler runtime status.

### Changed

- Enforced slot-local committed WSPR payloads.
- Moved paired-frame progress ownership into scheduler state.
- Removed stale paired failure status references and aligned planner handling
  with WSPR-Reference paired semantics.
- Removed the old `-15` WSPR alias surface and aligned UI frequency validation
  with backend token parsing.
- Made WSPR Dial Frequency Set internal-only and removed it from public config
  surfaces.

### Fixed

- Fixed paired Type2Type3 WSPR frame 2 crash caused by treating a global frame
  index as an index into a slot-local payload.
- Fixed WSPR startup configuration and scheduling flow regressions.
- Fixed PPM consumption semantics.
- Fixed WSPR dial-frequency versus RF-frequency handling.

## CW, QRSS, FSKCW, And DFCW

### Added

- Added CW timing gap controls for intra-element, inter-character, and
  inter-word spacing.
- Added CW fade shape, fade-in, fade-out, and fade slice configuration.
- Added linear and raised-cosine fade shaping for QRSS, FSKCW, and DFCW.
- Added live CW runtime message reporting and active-character progress.
- Added next-message timing display for idle CW operation.

### Changed

- Unified CW base-frequency default to 14.0969 MHz.
- Aligned CW shift default to 5 Hz across UI, backend, and INI defaults.
- Treated CW frequency offset as whole-number Hz in the UI.
- Normalized QRSS, FSKCW, and DFCW message input by trimming leading/trailing
  whitespace while preserving internal spaces.
- Allowed all canonical frequencies for CW modes.
- Hid advanced fade controls from the normal UI while preserving config support.

### Fixed

- Fixed QRSS message truncation after the first character in the Raspberry Pi
  backend by preserving the DMA/PWM execution context across RF-off gaps.
- Fixed Si5351 CW tail truncation on the final event.
- Fixed GPIO CW continuous tone emission by disabling fade slicing on GPIO RF-on
  events.
- Fixed CW base-frequency unit serialization in the UI.
- Fixed CW dot-length HTML constraints to match backend validation.
- Rejected QRSS, FSKCW, and DFCW messages that exceed `repeat_every`.

## GPIO And LPF Selector Handling

### Added

- Added per-entry `@GPIO`, `@GPIOH`, and `@GPIOL` selector support.
- Added selector GPIO snapshots to committed transmission requests.
- Added scheduler regression coverage for startup baselining, multi-slot
  sequencing, mixed-polarity idle state, and committed-request restore behavior.

### Changed

- Unified GPIO selection under a single Band GPIO selector model.
- Made selector source explicit: per-entry override, configured Band GPIO
  fallback, or no selector.
- Kept configured selector GPIOs requested and driven inactive between
  transmissions.
- Reapplied committed LPF selector snapshots at every transmission start.
- Made Band GPIO defaults explicit but disabled for every band.
- Disabled implicit Band GPIO fallback for plain WSPR frequency entries.

### Fixed

- Fixed frequency rotations where only the first selector GPIO was asserted.
- Fixed selector reuse by forcing the active selector inactive before reuse.
- Suppressed misleading selector assert noise when no selector is active.

## Configuration, CLI, And INI Behavior

### Added

- Added backend-specific `[GPIO]` and `[Si5351]` config sections.
- Added canonical Operation-based configuration with `Operation.Mode` and
  `Operation.Transmit`.
- Added hex Si5351 I2C address parsing from INI and UI input.
- Added `--planner-preference auto|prefer_paired|require_paired`.
- Added `--release` installer/build option support.

### Changed

- Renamed the public/INI Runtime section to Operation.
- Moved Mode from Meta to Operation.
- Moved GPIO transmit pin, power level, and NTP setting into `[GPIO]`.
- Moved Si5351 settings into `[Si5351]`.
- Removed non-canonical UI/config compatibility paths.
- Clamped manual PPM to `[-200, 200]` during backend normalization.
- Defaulted missing GPIO Use NTP to true in the UI loader and backend
  normalization.
- Made direct CLI transmit intent work for Si5351 and tone modes outside
  INI-managed runs.

### Fixed

- Fixed config schema drift between the UI, backend JSON, and INI.
- Fixed mode-change persistence so guarded disable-and-switch flows do not write
  intermediate disabled states unnecessarily.
- Fixed direct Si5351 test-tone startup when persistent transmit is false.

## Spots And External Data

### Added

- Added downloader-first wspr.live auto mode with direct ClickHouse fallback.
- Added source-aware caching so downloader and direct query results do not
  collide.
- Added source/fallback metadata and a minimal UI source selector/status display.

### Changed

- Removed the experimental WSPRnet HTML scraping path.
- Normalized downloader and direct wspr.live responses to the existing table data
  contract.

## Logging, Diagnostics, And Maintenance

### Added

- Added persisted debug logging configuration and internal patch support.
- Added clearer platform/backend error wording at UI surfaces.
- Added more detailed GPIO resolver and runtime failure logs.

### Changed

- Normalized lifecycle stop logs and startup configuration logs.
- Reformatted log messages for clearer grammar and consistency.
- Avoided unwanted spaces after opening delimiters in variadic log output.
- Leveled debug logging off by default.
- Reworked Maintenance panels around repair, reset, and utility actions.

## Installer And Build

### Added

- Added Raspberry Pi 5 installer allowance.
- Added release-build resolution support for install/build flows.

### Changed

- Changed branch defaults during development and release preparation.
- Updated examples, defaults, and codebase map references.
- Updated submodules for WSPR-Reference, WSPR-Transmitter, and WsprryPi-UI.

### Fixed

- Fixed Bookworm/Pi 5 compile paths around libgpio and GPIO utilities.
- Fixed I2C reboot behavior in the installer.
- Fixed build/test behavior around GPIO resolver utilities.

## Compatibility Notes

- GPIO RF transmission is intentionally unavailable on Raspberry Pi 5 and
  unknown platforms. Use the Si5351 backend on those systems.
- The default UI entry point is now the Operation page, while saved settings are
  managed from Setup.
- Runtime configuration now uses the canonical Operation, GPIO, Si5351, WSPR,
  CW, and Calibration fields. Legacy/non-canonical public config paths were
  removed.
- WSPR frequency entries no longer get implicit Band GPIO fallback unless the
  entry or config explicitly enables the intended selector behavior.
- Si5351 TX output remains configurable by CLI and INI, but is not exposed in
  the web UI.
- Advanced CW fade settings remain supported in configuration but are hidden
  from the normal UI.

## Filtered From Release Notes

The raw commit range also includes routine version bumps, codebase map updates,
minor grammar-only log changes, repeated branch default changes during release
preparation, duplicate submodule-forward commits, and small cleanup-only commits.
Those were folded into the grouped sections above or omitted from the main
release text.

## Notable Commits

- `687f77e` Fix Pi 5 build support.
- `c3e7156` Add wspr.live direct fallback with downloader-first auto failover.
- `29f3ad7` Fix CW mode character progression.
- `2ac1686` Fix paired WSPR frame crash.
- `e56851d` UI review, hardening, and Operation page split.
- `1969ff7` CLI and configuration option updates.
- `e3c0382` UI enhancements, headless mode, and websocket correctness.
- `5c1ca10` Force selector GPIO inactive before reuse and between transmissions.
- `424be31` Deterministic LPF selector GPIO lifecycle management.
- `5de36c4` Fix ambiguous GPIO events.
- `ab70032` Improve shutdown ordering, RT scheduling fallback, and reload fixes.
- `30c4828` Installer Pi 5 and I2C reboot behavior fixes.
- `3c10c24` Restrict GPIO transmission to Raspberry Pi 4 and earlier.
- `a3cd0c3` Add selectable Si5351 backend support.
- `c01439b` Introduce canonical execution model, CW modes, and runtime control.
- `7f1447f` Introduce WSPR reference encoder path.
- `bd6461c` Refactor backend abstraction and WSPR encoding architecture.
