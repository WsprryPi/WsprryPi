# Legacy GPIO exact test-tone frequency

## Behavior and scope

The legacy GPIO TONE path in `WSPR-Transmitter` targets the committed RF carrier. The
committed frequency stays in the RF-carrier domain. The legacy plan adapter
uses zero spacing for TONE, so every table entry and the continuous symbol-zero
emitter target that frequency. Setup, start, and watchdog recovery reconstruct
the same plan without adding an offset to the request or status.

For example, a 14,097,100 Hz TONE request now targets 14,097,100 Hz. The affected
implementation at `654d51ab253fb5aaa26cf0ef3e8208cd2ffcfd6f` instead targeted
14,097,097.802734375 Hz because it used WSPR symbol zero. The 2.197265625 Hz
displacement is independent of the dial/audio offset and is not a clock PPM
constant. Identify the exact build when interpreting older measurements. Do not apply the old adjustment to corrected builds.

GPIO divider planning and table construction are pure functions in
`src/WSPR-Transmitter/src/gpio_frequency_plan.{hpp,cpp}`. Production DMA setup
uses that result directly, and the emitter uses the plan's shared symbol
frequency calculation. Clock-model composition and parent selection are
unchanged; they now receive the actual single-frequency range for TONE.

The existing safe-divider constraint requires both dither words to have the
same integer part. An exact TONE whose pair crosses that boundary fails during
configuration, before GPIO routing in table setup, rather than silently
substituting another carrier. Existing configuration-failure cleanup still
applies. Preparation may already have allocated hardware resources; this is
not a new claim of hardware-free configuration. Normal modulated tables retain
their existing hardware-limit center adjustment, with their divider words
checked before routing. Reported carrier values remain synthesis targets, not
measured RF.

WSPR continues to use four tones at -2.197265625, -0.732421875, +0.732421875,
and +2.197265625 Hz relative to its center. QRSS, FSKCW, and DFCW keep their
existing compatibility reference compensation. Si5351, RP1, simulated, and WTP
TONE plans receive no extra frequency correction. No calibration constants,
CLI options, UI, scheduler rules, selectors, or persistence formats changed.

## Validation boundary

`make legacy-tone-frequency-test SUDO=` from `src` exercises the pure production
plan adapter, table construction, divider limits and symbol-frequency mapping.
Hardware-free arithmetic checks do not establish RF frequency accuracy or
qualify a board, clock or physical output chain.
