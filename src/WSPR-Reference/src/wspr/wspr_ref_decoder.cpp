#include "wspr_ref_decoder.hpp"
#include "wspr_constants.hpp"
#include "wspr_ref_fano.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

namespace wspr
{
    bool WsprRefDecoder::symbols_to_bits(
        const std::string &symbol_text,
        uint8_t *g_bits,
        std::string &error) const
    {
        uint8_t symbols[WSPR_SYMBOL_COUNT] = {};

        error.clear();

        if (!parse_symbols(symbol_text, symbols, error))
            return false;

        for (std::size_t i = 0; i < WSPR_SYMBOL_COUNT; ++i)
        {
            if (symbols[i] < SYNC_VECTOR[i])
            {
                error = "Invalid symbol below sync value at index " + std::to_string(i);
                return false;
            }

            const uint8_t delta =
                static_cast<uint8_t>(symbols[i] - SYNC_VECTOR[i]);

            if ((delta & 0x01U) != 0)
            {
                error =
                    "Invalid symbol/sync combination at index " +
                    std::to_string(i);
                return false;
            }

            if (delta > 2)
            {
                error = "Invalid symbol range at index " + std::to_string(i);
                return false;
            }

            g_bits[i] = static_cast<uint8_t>(delta / 2);
        }

        return true;
    }

    void WsprRefDecoder::deinterleave_bits(
        const uint8_t *interleaved_bits,
        uint8_t *deinterleaved_bits) const
    {
        std::size_t i = 0;

        for (uint16_t j = 0; j < 255; ++j)
        {
            uint8_t index_temp = static_cast<uint8_t>(j);
            uint8_t rev = 0;

            for (uint8_t k = 0; k < 8; ++k)
            {
                if ((index_temp & 0x01U) != 0)
                    rev = static_cast<uint8_t>(rev | (1U << (7 - k)));

                index_temp = static_cast<uint8_t>(index_temp >> 1);
            }

            if (rev < WSPR_BIT_COUNT)
            {
                deinterleaved_bits[i] = interleaved_bits[rev];
                ++i;
            }

            if (i >= WSPR_BIT_COUNT)
                break;
        }
    }

    bool WsprRefDecoder::decode_payload_bits_from_symbols(
        const std::string &symbol_text,
        uint8_t *payload_bits,
        std::string &error) const
    {
        uint8_t g_bits[WSPR_BIT_COUNT] = {};
        uint8_t deinterleaved_bits[WSPR_BIT_COUNT] = {};

        error.clear();

        if (!symbols_to_bits(symbol_text, g_bits, error))
            return false;

        deinterleave_bits(g_bits, deinterleaved_bits);

        WsprRefFanoDecoder fano;
        return fano.decode_hard_bits(
            deinterleaved_bits,
            WSPR_BIT_COUNT,
            payload_bits,
            WSPR_PAYLOAD_BIT_COUNT,
            error);
    }

    bool WsprRefDecoder::parse_symbols(
        const std::string &symbol_text,
        uint8_t *symbols,
        std::string &error) const
    {
        std::size_t count = 0;
        error.clear();

        for (char c : symbol_text)
        {
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
                continue;

            if (c < '0' || c > '3')
            {
                error = std::string("Invalid character in symbol string: ") + c;
                return false;
            }

            if (count >= WSPR_SYMBOL_COUNT)
            {
                error = "Too many symbols in input.";
                return false;
            }

            symbols[count++] = static_cast<uint8_t>(c - '0');
        }

        if (count != WSPR_SYMBOL_COUNT)
        {
            error = "Expected 162 symbols, got " + std::to_string(count);
            return false;
        }

        return true;
    }
} // namespace wspr
