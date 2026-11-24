# Pico GPIO drive strength

The selected Pico supports 2, 4, 8, or 12 mA nominal direct RF GPIO drive, with 12 mA as the fresh-install default, only when its current Control/1 descriptor is advertised. Use Read GPIO drive in member hardware, select a value, Save GPIO drive, then explicitly restart the Pico through its supported management workflow. Saved, boot selection, and verified physical pad readback are separate observations. An unavailable or unreachable member never displays an observed default.

This electrical setting does not adjust WSPR reported power or any Pi local BCM/RP1/Si5351 setting. Actual RF power depends on load and frequency. Other pins and native execution semantics are unchanged. USB needs no Pico Wi-Fi. Network management requires the existing authenticated Consumer-Radio or engineering carrier. External WTP execution is retired.

Save drive progress retains this one field without a complete station. Apply saved drive and Save GPIO drive can persist drive settings before station commissioning when Pico storage is healthy. Saved drive is null before initial persistence. A saved configuration can have both station fields empty with WSPR disabled. A station edit still requires a valid resulting callsign, locator, and reported power. The protected GET/PUT/POST `/api/control/v1/targets/<device-id>/pico/gpio-drive-progress` facade forwards only the current progress drive field and explicit drive domain, preserving other drafts. Drive Apply returns the current progress document and confirms the matching saved value through a fresh drive readback.

The protected GET/PUT /api/control/v1/targets/<device-id>/pico/gpio-drive route binds a fresh read to the current selection and ETag. The adapter validates the complete `gpio-drive` descriptor, target and boot, current application context, and readback before forwarding one mutation. It reconciles the saved response but never replays a lost mutation. A rejected or uncertain save preserves the draft and requires explicit read/review before another save. Changing member or boot requires discarding the old member-bound draft before saving to the new member. Firmware that does not implement the current contract is refused; the adapter does not select historical settings views.

The ordinary Pico Si5351 profile `qrp-labs-rev6-25mhz-clk0` uses a separate nominal drive domain of 2, 4, 6, or 8 mA. The companion validates the boot-active value against the exact advertised profile and verifies the other profile and boot fields. Settings saves preserve the distinction between saved and active hardware.

## Qualification boundary

Nominal drive selection does not establish measured RF amplitude, power, or spectral performance. Physical pad programming, restart/re-enumeration and RF behavior require evidence for the selected firmware, board, output, and load.
