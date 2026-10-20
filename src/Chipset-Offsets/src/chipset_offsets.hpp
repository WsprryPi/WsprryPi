#pragma once

#include <stdexcept>

namespace wsprrypi
{
enum class ClockChipset
{
    Bcm2835,
    Bcm2836Bcm2837,
    Bcm2711,
    Rp1
};

// Intrinsic RF-parent correction, additional to the selected system/manual
// correction. Positive means fast. This is the sole chipset offset table.
constexpr double chipsetIntrinsicOffsetPpm(ClockChipset chipset)
{
    switch (chipset)
    {
    case ClockChipset::Bcm2835:
        return -2.5; // Historical Pi1 baseline.
    case ClockChipset::Bcm2836Bcm2837:
    case ClockChipset::Bcm2711:
        return 0.0; // Discovery baseline, not a measured zero error.
    case ClockChipset::Rp1:
        // Operator-approved universal default: rounded equal-band mean of
        // the Issue 429 fourteen-point wspr5 GPIO20 sweep. Not all-board proof.
        return -46.245;
    }
    throw std::invalid_argument("Unsupported clock chipset offset.");
}
}
