#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    std::string decode_one_char(uint32_t value)
    {
        if (value <= 9U)
            return "/" + std::string(1, static_cast<char>('0' + value));

        if (value >= 10U && value <= 35U)
            return "/" + std::string(1, static_cast<char>('A' + (value - 10U)));

        if (value == 38U)
            return "/?";

        return {};
    }

    std::string decode_two_digit(uint32_t value)
    {
        if (value > 99U)
            return {};

        std::string out = "/";
        out.push_back(static_cast<char>('0' + (value / 10U)));
        out.push_back(static_cast<char>('0' + (value % 10U)));
        return out;
    }
} // namespace

int main()
{
    std::cout << "Type 2 overlap observation:\n\n";

    for (uint32_t ext_field = 27258U; ext_field <= 27270U; ++ext_field)
    {
        const uint32_t one_char_value = ext_field - 27232U;
        const uint32_t two_digit_value = ext_field - 27258U;

        const std::string one_char = decode_one_char(one_char_value);
        const std::string two_digit = decode_two_digit(two_digit_value);

        std::cout
            << "ext_field=" << ext_field
            << "  one-char=" << (one_char.empty() ? "<invalid>" : one_char)
            << "  two-digit=" << (two_digit.empty() ? "<invalid>" : two_digit)
            << "\n";
    }

    return 0;
}
