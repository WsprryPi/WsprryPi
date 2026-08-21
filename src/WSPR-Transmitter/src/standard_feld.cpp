#include "standard_feld.hpp"

#include "standard_feld_asset.hpp"

#include <stdexcept>

namespace wsprrypi::standard_feld
{

std::string normalize_message(std::string_view message)
{
    if (message.empty())
        throw std::runtime_error("Standard Feld payload message is empty.");

    std::string normalized;
    normalized.reserve(message.size());

    for (std::size_t i = 0; i < message.size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(message[i]);
        if (byte >= 'a' && byte <= 'z')
        {
            normalized.push_back(static_cast<char>(byte - 'a' + 'A'));
            continue;
        }

        if (byte < kFirstCodePoint || byte > kLastCodePoint)
        {
            throw std::runtime_error(
                "Standard Feld payload contains unsupported input at byte " +
                std::to_string(i) + ".");
        }

        normalized.push_back(static_cast<char>(byte));
    }

    return normalized;
}

} // namespace wsprrypi::standard_feld
