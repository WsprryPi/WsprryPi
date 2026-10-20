#include "chipset_offsets.hpp"

#include <iostream>
#include <stdexcept>

int main()
{
    using wsprrypi::ClockChipset;
    using wsprrypi::chipsetIntrinsicOffsetPpm;
    static_assert(chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2835) == -2.5);
    static_assert(chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2836Bcm2837) == 0.0);
    static_assert(chipsetIntrinsicOffsetPpm(ClockChipset::Bcm2711) == 0.0);
    static_assert(chipsetIntrinsicOffsetPpm(ClockChipset::Rp1) == -46.245);
    try
    {
        (void)chipsetIntrinsicOffsetPpm(static_cast<ClockChipset>(999));
    }
    catch (const std::invalid_argument&)
    {
        std::cout << "Chipset offset selector tests passed\n";
        return 0;
    }
    std::cerr << "Unknown chipset must fail closed\n";
    return 1;
}
