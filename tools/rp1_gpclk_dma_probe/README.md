# Legacy RP1 GPCLK clock-disabled DMA probe

This retained research probe uses a fixed **50 MHz GPCLK parent** and a
four-tone divider table calculated for that parent. It does not implement the
current RP1-GPCLK-DKMS adapter, which uses nominal **200 MHz `pll_sys`**.
The probe checks the observed parent rate before acquiring exclusive clock
rate ownership and again after its inactive seed-rate request, which can
reparent GPCLK0. Other parent rates are rejected before DMA submission.
Its required initial 50 MHz GPCLK output rate is a separate baseline check;
an output rate alone does not identify the parent.

The probe never prepares or enables GPCLK0, and its overlay declares no GPIO
or pinctrl configuration. It accesses the DMA and divider resources directly
and uses internal provider probes for readback. These research interfaces are
outside the production module contract; the parent-rate check does not
establish resource isolation or target qualification.

The fixture uses 66,792 transfers at the 511-cycle DMA tick. DMA pacing has
its own 50 MHz reference, independent of the GPCLK parent. Logical 16-bit
fractions are packed into bits 31:16 of `DIV_FRAC` for DMA and provider
readback comparisons.

Cancellation prevents successor work and drains the current finite descriptor
before disabling the tick/DREQ and restoring the output rate. This fixed
research descriptor is approximately one WSPR symbol; production DKMS uses
its own bounded chunk contract. The probe requires separate target
authorization and validation before any use.
