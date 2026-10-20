#ifndef _UTILS_HPP
#define _UTILS_HPP

#include <array>
#include <string_view>

constexpr std::array<char, 19> make_log_tag_chars(std::string_view name)
{
    std::array<char, 19> out{};

    out[0] = '[';

    std::size_t i = 0;
    for (; i < name.size() && i < 16; ++i)
        out[i + 1] = name[i];

    for (; i < 16; ++i)
        out[i + 1] = ' ';

    out[17] = ']';
    out[18] = ' ';

    return out;
}

template <std::size_t N>
constexpr std::string_view as_string_view(const std::array<char, N> &a)
{
    return std::string_view(a.data(), a.size());
}

double get_ppm_from_chronyc();

#endif // _UTILS_HPP
