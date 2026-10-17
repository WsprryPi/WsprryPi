# Pico GPIO drive strength

The selected Pico supports 2, 4, 8 or 12 mA nominal direct RF GPIO drive,
with 4 mA as default, only when its exact versioned descriptor is advertised.
Use Read GPIO drive in member hardware, select a value, Save GPIO drive, then
explicitly restart the Pico through its supported management workflow. Saved,
boot selection and verified physical pad readback are separate observations.
An unavailable or unreachable member never displays an observed default.

This electrical setting does not adjust WSPR reported power or any Pi local
BCM/RP1/Si5351 setting. Actual RF power depends on load and frequency. Other
pins and WTP/1 jobs are unchanged. USB needs no Pico Wi-Fi. Network management
requires the existing authenticated Consumer-Radio or engineering carrier.
Plain WTP transport provides no configuration authority.

Save drive progress retains this one field without a complete station. Apply saved drive is a separate action over a complete applied configuration. The protected GET/PUT/POST `/api/v1/member/gpio-drive-progress` facade forwards only the negotiated progress/2 drive field and explicit drive domain, preserving other drafts.

The protected GET/PUT /api/v1/member/gpio-drive route binds a fresh read to the
current selection and ETag. The adapter validates the complete
transmitter-gpio-drive/1 descriptor, target/boot, old application context and
readback before forwarding one mutation. It reconciles the saved response but
never replays a lost mutation. A rejected or uncertain save preserves the draft
and requires explicit read/review before another save. Changing member or boot
requires discarding the old member-bound draft before saving to the new member.
Older firmware shows drive unavailable and retains its other usable settings.

## Documentation Impact

Updated this companion development contract. Existing USB and configuration
operator guides retain their original scope and link here for the extension.
P15.20 must add the four choices/default, electrical meaning, saved versus active
status, explicit restart, old-firmware availability and unknown-outcome recovery.
The separate operator documentation repository/site is outside this task.
Physical pad programming, USB restart/re-enumeration and RF amplitude/spectral
acceptance remain separately recorded in the Pico P15.15 execution record.
