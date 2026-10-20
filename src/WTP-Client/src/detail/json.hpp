// SPDX-License-Identifier: MIT
// Derived from WsprryPico; Copyright (c) 2026 Lee Bussy. See LICENSE.md and PROVENANCE.json.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wsprrypi::wtp::json {
// Non-owning views into one validated, immutable payload. No DOM is retained.
struct Value {
    std::string_view raw;
    char type() const;
    std::string string() const;
    std::int32_t integer() const;
    bool boolean() const;
    std::vector<Value> elements(std::size_t limit = 512) const;
    std::optional<Value> get(std::string_view key) const;
};
std::optional<Value> parse(std::string_view payload);
std::string quote(std::string_view value);
bool decimal(Value value, std::uint64_t &output, bool nonzero = false);
bool identifier(Value value);
bool fields(Value value, std::initializer_list<std::string_view> required,
            std::initializer_list<std::string_view> optional = {});
} // namespace wsprrypi::wtp::json
