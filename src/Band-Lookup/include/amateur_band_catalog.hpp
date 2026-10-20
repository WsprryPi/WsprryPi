#ifndef WSPRRYPI_AMATEUR_BAND_CATALOG_HPP
#define WSPRRYPI_AMATEUR_BAND_CATALOG_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace wsprrypi::bands
{
struct BandDefinition
{
    std::string_view canonical_name;
    std::string_view display_name;
    long long lower_hz;
    long long upper_hz;
};

// Inclusive correlation envelopes for ordinary amateur bands allocated in at
// least one jurisdiction. They are not statements of operating authority.
inline constexpr std::array<BandDefinition, 19> catalog{{
    {"2200m", "2200 m", 130000LL, 190000LL},
    {"630m", "630 m", 472000LL, 479000LL},
    {"160m", "160 m", 1800000LL, 2000000LL},
    {"80m", "80 m", 3500000LL, 4000000LL},
    {"60m", "60 m", 5250000LL, 5450000LL},
    {"40m", "40 m", 7000000LL, 7300000LL},
    {"30m", "30 m", 10100000LL, 10150000LL},
    {"20m", "20 m", 14000000LL, 14350000LL},
    {"17m", "17 m", 18068000LL, 18168000LL},
    {"15m", "15 m", 21000000LL, 21450000LL},
    {"12m", "12 m", 24890000LL, 24990000LL},
    {"10m", "10 m", 28000000LL, 29700000LL},
    {"8m", "8 m", 40000000LL, 45000000LL},
    {"6m", "6 m", 50000000LL, 54000000LL},
    // The shared 54 MHz edge belongs to 6m so every frequency has one identity.
    {"5m", "5 m", 54000001LL, 68000000LL},
    {"4m", "4 m", 69900000LL, 70500000LL},
    {"2m", "2 m", 144000000LL, 148000000LL},
    {"1.25m", "1.25 m", 219000000LL, 225000000LL},
    {"70cm", "70 cm", 420000000LL, 450000000LL},
}};

inline constexpr const BandDefinition *find(double frequency_hz) noexcept
{
    for (const auto &band : catalog)
        if (frequency_hz >= band.lower_hz && frequency_hz <= band.upper_hz)
            return &band;
    return nullptr;
}

inline constexpr std::optional<std::size_t> find_index(double frequency_hz) noexcept
{
    for (std::size_t index = 0; index < catalog.size(); ++index)
        if (frequency_hz >= catalog[index].lower_hz &&
            frequency_hz <= catalog[index].upper_hz)
            return index;
    return std::nullopt;
}
} // namespace wsprrypi::bands

#endif
