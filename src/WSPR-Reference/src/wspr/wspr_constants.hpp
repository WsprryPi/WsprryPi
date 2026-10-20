#ifndef WSPR_CONSTANTS_HPP
#define WSPR_CONSTANTS_HPP

/// \file wspr_constants.hpp
/// \brief Public constants shared across the WSPR reference implementation.

#include <cstddef>
#include <cstdint>

namespace wspr
{
    /// \brief Number of WSPR channel symbols per message.
    constexpr std::size_t WSPR_SYMBOL_COUNT = 162;
    /// \brief Number of convolutionally encoded bits per message.
    constexpr std::size_t WSPR_BIT_COUNT = 162;
    /// \brief Number of uncoded payload bits per message.
    constexpr std::size_t WSPR_PAYLOAD_BIT_COUNT = 81;
    /// \brief First WSPR convolutional encoder polynomial.
    constexpr uint32_t WSPR_POLY_0 = 0xf2d05351U;
    /// \brief Second WSPR convolutional encoder polynomial.
    constexpr uint32_t WSPR_POLY_1 = 0xe4613c47U;

    /// \brief Fixed WSPR synchronization vector applied to every encoded message.
    inline constexpr uint8_t SYNC_VECTOR[WSPR_SYMBOL_COUNT] = {
        1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0,
        0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1,
        0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0,
        1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1,
        1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0};

} // namespace wspr

#endif // WSPR_CONSTANTS_HPP
