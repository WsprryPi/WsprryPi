# Raspberry Pi RP1 PIO userspace header

`rp1_pio_if.h` is an unmodified public Linux userspace interface header from
Raspberry Pi Linux revision `4103a989a46196239b830bd07693277e42b8f992`.
`PROVENANCE.json` records its upstream path and the SHA-256 of every imported
file. The original SPDX notice, GPL-2.0 license, and Linux syscall exception
are retained. This import does not change the application's MIT license.

The parent PPS adapter uses state-machine allocation, program references,
initialization, nonblocking FIFO reads/writes, and enable/close lifecycle.
It does not use raw register access, GPIO configuration, DMA, interrupts, or
the newer firmware-only FIFO-flag query. Importing the public header makes
Debian cross-builds independent of the build host's Raspberry Pi headers;
runtime device, board, input, clock, and ownership checks remain mandatory.
