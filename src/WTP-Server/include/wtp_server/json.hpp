#pragma once

#include "wtp_server/input_view.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wsprrypico::wtp::json {
// Non-owning views into one validated, immutable payload. No DOM is retained.
struct Value {
    InputView raw;
    char type() const;
    std::string string() const;
    std::int32_t integer() const;
    bool boolean() const;
    // Traverse a validated array without retaining element views. Start cursor at zero.
    std::optional<Value> next_element(std::size_t& cursor) const;
    std::vector<Value> elements(std::size_t limit = 512) const;
    std::optional<Value> get(std::string_view key) const;
};
std::optional<Value> parse(InputView payload);
std::string quote(std::string_view value);
bool decimal(Value value, std::uint64_t& output, bool nonzero = false);
bool identifier(Value value);
bool fields(Value value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional = {});
} // namespace wsprrypico::wtp::json
