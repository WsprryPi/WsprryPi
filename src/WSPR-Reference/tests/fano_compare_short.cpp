#include "wspr/wspr_ref_fano.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void encode_payload_bits(
        const wspr::WsprRefFanoDecoder &fano,
        const std::vector<uint8_t> &payload_bits,
        std::vector<uint8_t> &coded_bits)
    {
        coded_bits.clear();
        coded_bits.reserve(payload_bits.size() * 2U);

        uint32_t shift_register = 0;

        for (uint8_t bit : payload_bits)
        {
            uint8_t p0 = 0;
            uint8_t p1 = 0;

            fano.debug_expected_parity(shift_register, bit, p0, p1);

            coded_bits.push_back(p0);
            coded_bits.push_back(p1);

            shift_register =
                (shift_register << 1) | static_cast<uint32_t>(bit);
        }
    }

    bool compare_bits(
        const std::vector<uint8_t> &a,
        const std::vector<uint8_t> &b)
    {
        if (a.size() != b.size())
            return false;

        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] != b[i])
                return false;
        }

        return true;
    }

    void print_bits(const std::string &label, const std::vector<uint8_t> &bits)
    {
        std::cout << label << "\n";
        for (uint8_t bit : bits)
            std::cout << static_cast<unsigned>(bit);
        std::cout << "\n\n";
    }
} // namespace

int main()
{
    wspr::WsprRefFanoDecoder fano;

    const std::vector<uint8_t> payload_bits = {
        1, 0, 1, 1, 0, 0, 1, 0,
        1, 1, 1, 0, 0, 1, 0, 1};

    std::vector<uint8_t> coded_bits;
    encode_payload_bits(fano, payload_bits, coded_bits);

    std::vector<uint8_t> bounded_bits(payload_bits.size(), 0);
    std::vector<uint8_t> limited_bits(payload_bits.size(), 0);

    std::string bounded_error;
    std::string limited_error;

    const bool bounded_ok =
        fano.decode_hard_bits_bounded(
            coded_bits.data(),
            coded_bits.size(),
            bounded_bits.data(),
            bounded_bits.size(),
            payload_bits.size(),
            bounded_error);

    const bool limited_ok =
        fano.decode_hard_bits_fano_limited(
            coded_bits.data(),
            coded_bits.size(),
            limited_bits.data(),
            limited_bits.size(),
            payload_bits.size(),
            limited_error);

    if (!bounded_ok)
    {
        std::cerr << "Bounded decode failed: " << bounded_error << "\n";
        return 1;
    }

    if (!limited_ok)
    {
        std::cerr << "Limited decode failed: " << limited_error << "\n";
        return 1;
    }

    print_bits("Payload bits:", payload_bits);
    print_bits("Coded bits:", coded_bits);
    print_bits("Bounded decoded bits:", bounded_bits);
    print_bits("Limited decoded bits:", limited_bits);

    const bool bounded_matches_input = compare_bits(payload_bits, bounded_bits);
    const bool limited_matches_input = compare_bits(payload_bits, limited_bits);
    const bool bounded_matches_limited = compare_bits(bounded_bits, limited_bits);

    std::cout
        << "bounded_matches_input=" << (bounded_matches_input ? "true" : "false") << "\n"
        << "limited_matches_input=" << (limited_matches_input ? "true" : "false") << "\n"
        << "bounded_matches_limited=" << (bounded_matches_limited ? "true" : "false") << "\n\n";

    const bool pass =
        bounded_matches_input &&
        limited_matches_input &&
        bounded_matches_limited;

    std::cout << "Summary: " << (pass ? "PASS" : "FAIL") << "\n";
    return pass ? 0 : 1;
}
