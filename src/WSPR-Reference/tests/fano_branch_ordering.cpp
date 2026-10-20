#include "wspr/wspr_ref_fano.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    wspr::WsprRefFanoDecoder fano;

    const std::vector<uint8_t> input_bits = {1, 1, 0, 1, 0, 0, 1, 0};

    uint32_t shift_register = 0;

    std::cout << "Input bits:\n";
    for (uint8_t b : input_bits)
        std::cout << static_cast<unsigned>(b);
    std::cout << "\n\n";

    std::cout << "Branch ordering sanity check:\n";

    bool all_good = true;

    for (std::size_t i = 0; i < input_bits.size(); ++i)
    {
        const uint8_t true_bit = input_bits[i];
        const uint8_t false_bit = static_cast<uint8_t>(1U - true_bit);

        uint8_t observed_p0 = 0;
        uint8_t observed_p1 = 0;
        uint8_t true_p0 = 0;
        uint8_t true_p1 = 0;
        uint8_t false_p0 = 0;
        uint8_t false_p1 = 0;

        fano.debug_expected_parity(
            shift_register,
            true_bit,
            observed_p0,
            observed_p1);

        fano.debug_expected_parity(
            shift_register,
            true_bit,
            true_p0,
            true_p1);

        fano.debug_expected_parity(
            shift_register,
            false_bit,
            false_p0,
            false_p1);

        const int true_metric = fano.debug_branch_metric_hard(
            true_p0,
            true_p1,
            observed_p0,
            observed_p1);

        const int false_metric = fano.debug_branch_metric_hard(
            false_p0,
            false_p1,
            observed_p0,
            observed_p1);

        const bool preferred = (true_metric >= false_metric);
        if (!preferred)
            all_good = false;

        std::cout
            << "depth " << i
            << ": state=0x" << std::hex << shift_register << std::dec
            << " observed=("
            << static_cast<unsigned>(observed_p0) << ","
            << static_cast<unsigned>(observed_p1) << ")"
            << " true_bit=" << static_cast<unsigned>(true_bit)
            << " true_metric=" << true_metric
            << " false_bit=" << static_cast<unsigned>(false_bit)
            << " false_metric=" << false_metric
            << " result=" << (preferred ? "OK" : "BAD")
            << "\n";

        shift_register =
            (shift_register << 1) | static_cast<uint32_t>(true_bit);
    }

    std::cout << "\nSummary: " << (all_good ? "PASS" : "FAIL") << "\n";

    return all_good ? 0 : 1;
}
