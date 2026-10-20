# RP1 GPCLK clock-disabled DMA probe

This research-only kernel probe tests whether RP1's DMA0 tick can pace a
preloaded sequence targeted at GPCLK0's fractional-divider register.
It acquires `clk_gp0` with exclusive rate ownership but deliberately never
prepares or enables it. The overlay declares no GPIO or pinctrl configuration.

This is not a production register-access interface. A production backend must
move DMA setup and divider-register ownership into the RP1 clock provider so
that DMA writes participate in that driver's locking and lifecycle contract.

The checked-in fixture uses 66,792 transfers at the slowest 511-cycle DMA tick.
Phase 6C completed all four clock-disabled descriptors within 4.8 us of one
WSPR symbol. It also corrected the register packing: RP1 expects the logical 16-bit
fraction in bits 31:16 of `DIV_FRAC`. With that shift, exact DMA readback and
the existing provider's raw regmap reads both matched all four final words.

Phase 6D uses bounded finite completion for cancellation. Once a symbol
descriptor has started, cancellation prevents future work but drains that one
finite descriptor to its normal hardware-idle completion before disabling the
tick/DREQ and restoring the divider. The hard bound is the remainder of one
0.682667-second WSPR symbol. The earlier tick-first abort ordering is documented
in the Phase 6C report and must not be restored.
