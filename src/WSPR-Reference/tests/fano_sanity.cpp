#include "wspr/wspr_ref_fano.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    wspr::WsprRefFanoDecoder fano;

    std::vector<uint8_t> input_bits = {1, 1, 0, 1, 0, 0, 1, 0};

    uint32_t shift_register = 0;

    std::cout << "Input bits:\n";
    for (uint8_t b : input_bits)
        std::cout << static_cast<unsigned>(b);
    std::cout << "\n\n";

    std::cout << "Expected parity pairs:\n";

    for (std::size_t i = 0; i < input_bits.size(); ++i)
    {
        uint8_t p0 = 0;
        uint8_t p1 = 0;

        fano.debug_expected_parity(shift_register, input_bits[i], p0, p1);

        std::cout
            << "bit[" << i << "]="
            << static_cast<unsigned>(input_bits[i])
            << "  -> parity=("
            << static_cast<unsigned>(p0)
            << ","
            << static_cast<unsigned>(p1)
            << ")\n";

        shift_register =
            (shift_register << 1) | static_cast<uint32_t>(input_bits[i]);
    }

    std::cout << "\nBranch metric examples:\n";

    std::cout
        << "match (1,0) vs (1,0): "
        << fano.debug_branch_metric_hard(1, 0, 1, 0)
        << "\n";

    std::cout
        << "one mismatch (1,0) vs (1,1): "
        << fano.debug_branch_metric_hard(1, 0, 1, 1)
        << "\n";

    std::cout
        << "full mismatch (1,0) vs (0,1): "
        << fano.debug_branch_metric_hard(1, 0, 0, 1)
        << "\n";

    return 0;
}
