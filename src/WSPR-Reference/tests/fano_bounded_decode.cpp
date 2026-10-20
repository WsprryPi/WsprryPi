#include "wspr/wspr_ref_fano.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void encode_known_bits(
    const wspr::WsprRefFanoDecoder& fano,
    const std::vector<uint8_t>& input_bits,
    std::vector<uint8_t>& coded_bits)
{
    coded_bits.clear();
    coded_bits.reserve(input_bits.size() * 2U);

    uint32_t shift_register = 0;

    for (uint8_t bit : input_bits)
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
} // namespace

int main()
{
    wspr::WsprRefFanoDecoder fano;

    const std::vector<uint8_t> input_bits = {
        1, 1, 0, 1, 0, 0, 1, 0
    };

    std::vector<uint8_t> coded_bits;
    encode_known_bits(fano, input_bits, coded_bits);

    std::vector<uint8_t> decoded_bits(input_bits.size(), 0);
    std::string error;

    if (!fano.decode_hard_bits_bounded(
            coded_bits.data(),
            coded_bits.size(),
            decoded_bits.data(),
            decoded_bits.size(),
            input_bits.size(),
            error))
    {
        std::cerr << "Bounded decode failed: " << error << "\n";
        return 1;
    }

    std::cout << "Input bits:\n";
    for (uint8_t bit : input_bits)
        std::cout << static_cast<unsigned>(bit);
    std::cout << "\n\n";

    std::cout << "Coded bits:\n";
    for (uint8_t bit : coded_bits)
        std::cout << static_cast<unsigned>(bit);
    std::cout << "\n\n";

    std::cout << "Decoded bits:\n";
    for (uint8_t bit : decoded_bits)
        std::cout << static_cast<unsigned>(bit);
    std::cout << "\n\n";

    bool match = true;
    for (std::size_t i = 0; i < input_bits.size(); ++i)
    {
        if (input_bits[i] != decoded_bits[i])
        {
            match = false;
            break;
        }
    }

    std::cout << "Summary: " << (match ? "PASS" : "FAIL") << "\n";
    return match ? 0 : 1;
}
