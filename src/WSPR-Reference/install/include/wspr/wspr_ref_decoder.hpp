#ifndef WSPR_REF_DECODER_HPP
#define WSPR_REF_DECODER_HPP

/// \file wspr_ref_decoder.hpp
/// \brief Public decoder helpers for symbol parsing and payload recovery.

#include <cstddef>
#include <cstdint>
#include <string>

namespace wspr
{
    /// \brief Reference decoder for converting symbol text back into payload bits.
    class WsprRefDecoder
    {
    public:
        /// \brief Convert a textual symbol stream into hard decision coded bits.
        /// \param symbol_text Input symbol text.
        /// \param g_bits Receives the recovered coded bits.
        /// \param error Receives the failure reason on error.
        /// \return True if the symbol stream parsed successfully.
        bool symbols_to_bits(
            const std::string &symbol_text,
            uint8_t *g_bits,
            std::string &error) const;

        /// \brief Reverse the WSPR interleaver on a coded bit stream.
        /// \param interleaved_bits Input interleaved coded bits.
        /// \param deinterleaved_bits Receives the deinterleaved bit stream.
        void deinterleave_bits(
            const uint8_t *interleaved_bits,
            uint8_t *deinterleaved_bits) const;

        /// \brief Decode payload bits directly from a textual symbol stream.
        /// \param symbol_text Input symbol text.
        /// \param payload_bits Receives the recovered payload bits.
        /// \param error Receives the failure reason on error.
        /// \return True if payload recovery succeeded, false otherwise.
        bool decode_payload_bits_from_symbols(
            const std::string &symbol_text,
            uint8_t *payload_bits,
            std::string &error) const;

    private:
        bool parse_symbols(
            const std::string &symbol_text,
            uint8_t *symbols,
            std::string &error) const;
    };
} // namespace wspr

#endif // WSPR_REF_DECODER_HPP
