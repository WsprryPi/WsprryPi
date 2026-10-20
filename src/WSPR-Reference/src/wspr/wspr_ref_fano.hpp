#ifndef WSPR_REF_FANO_HPP
#define WSPR_REF_FANO_HPP

/// \file wspr_ref_fano.hpp
/// \brief Public hard-decision Fano decoder used by the reference decoder.

#include <cstddef>
#include <cstdint>
#include <string>

namespace wspr
{
    /// \brief Hard-decision Fano decoder for the WSPR convolutional code.
    class WsprRefFanoDecoder
    {
    public:
        /// \brief Decode a full coded bit stream using the default Fano search.
        bool decode_hard_bits(
            const uint8_t *coded_bits,
            std::size_t coded_bit_count,
            uint8_t *decoded_bits,
            std::size_t decoded_bit_count,
            std::string &error) const;

        /// \brief Decode while limiting the number of input bits considered.
        bool decode_hard_bits_bounded(
            const uint8_t *coded_bits,
            std::size_t coded_bit_count,
            uint8_t *decoded_bits,
            std::size_t decoded_bit_count,
            std::size_t input_bit_limit,
            std::string &error) const;

        /// \brief Decode with a limited-backtracking Fano search variant.
        bool decode_hard_bits_fano_limited(
            const uint8_t *coded_bits,
            std::size_t coded_bit_count,
            uint8_t *decoded_bits,
            std::size_t decoded_bit_count,
            std::size_t input_bit_limit,
            std::string &error) const;

        /// \brief Expose expected parity bits for a hypothetical branch.
        void debug_expected_parity(
            uint32_t shift_register,
            uint8_t input_bit,
            uint8_t &p0,
            uint8_t &p1) const;

        /// \brief Expose the hard-decision branch metric used by the decoder.
        int debug_branch_metric_hard(
            uint8_t expected_p0,
            uint8_t expected_p1,
            uint8_t observed_p0,
            uint8_t observed_p1) const;

    private:
        struct FanoNode
        {
            uint32_t shift_register = 0;
            int cumulative_metric = 0;
            uint8_t chosen_bit = 0;
            uint8_t next_branch_to_try = 0;
        };

        void expected_parity(
            uint32_t shift_register,
            uint8_t input_bit,
            uint8_t &p0,
            uint8_t &p1) const;

        int branch_metric_hard(
            uint8_t expected_p0,
            uint8_t expected_p1,
            uint8_t observed_p0,
            uint8_t observed_p1) const;

        bool decode_hard_bits_fano_core(
            const uint8_t *coded_bits,
            std::size_t coded_bit_count,
            uint8_t *decoded_bits,
            std::size_t decoded_bit_count,
            std::size_t input_bit_limit,
            std::size_t max_backtracks,
            std::string &error) const;
    };
} // namespace wspr

#endif // WSPR_REF_FANO_HPP
