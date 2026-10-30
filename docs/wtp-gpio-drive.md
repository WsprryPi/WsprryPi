# Pico GPIO drive strength

The selected Pico supports 2, 4, 8 or 12 mA nominal direct RF GPIO drive,
with 4 mA as default, only when its current Control/1 descriptor is advertised.
Use Read GPIO drive in member hardware, select a value, Save GPIO drive, then
explicitly restart the Pico through its supported management workflow. Saved,
boot selection and verified physical pad readback are separate observations.
An unavailable or unreachable member never displays an observed default.

This electrical setting does not adjust WSPR reported power or any Pi local
BCM/RP1/Si5351 setting. Actual RF power depends on load and frequency. Other
pins and WTP/1 jobs are unchanged. USB needs no Pico Wi-Fi. Network management
requires the existing authenticated Consumer-Radio or engineering carrier.
Plain WTP transport provides no configuration authority.

Save drive progress retains this one field without a complete station. Apply saved drive is a separate action over a complete applied configuration. The protected GET/PUT/POST `/api/control/v1/member/gpio-drive-progress` facade forwards only the current progress drive field and explicit drive domain, preserving other drafts.

The protected GET/PUT /api/control/v1/member/gpio-drive route binds a fresh read to the
current selection and ETag. The adapter validates the complete
`gpio-drive` descriptor, target/boot, current application context and
readback before forwarding one mutation. It reconciles the saved response but
never replays a lost mutation. A rejected or uncertain save preserves the draft
and requires explicit read/review before another save. Changing member or boot
requires discarding the old member-bound draft before saving to the new member.
Firmware that does not implement the current contract is refused; the adapter does not select historical settings views.

## Qualification boundary

Nominal drive selection does not establish measured RF amplitude, power or
spectral performance. Physical pad programming, restart/re-enumeration and RF
behavior require evidence for the selected firmware, board, output and load.
