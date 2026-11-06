/* SPDX-License-Identifier: MIT */
#pragma once
#include "rp1_gpclk_uapi.h"
#include <string_view>
#include <string>
#include <regex>
#include <cstdint>

namespace wsprrypi {
// Interface/limit/capability checks admit the provider. Product identity is
// bounded SemVer data, with the route identity format defined by the UAPI contract.
struct Rp1GpclkConsumerContract {
    static constexpr std::string_view repository = "WsprryPi/RP1-GPIO-CLK-DKMS";
    static constexpr std::string_view module = "rp1-gpclk-dkms";
    static bool validProduct(std::string_view product) {
        if (product.empty() || product.size() > 48) return false;
        static const std::regex syntax(
            "(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
            "(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?");
        if (!std::regex_match(product.begin(), product.end(), syntax)) return false;
        const auto dash = product.find('-');
        const auto plus = product.find('+');
        if (dash == std::string_view::npos || (plus != std::string_view::npos && dash > plus)) return true;
        auto pre = product.substr(dash + 1,
            plus == std::string_view::npos ? plus : plus - dash - 1);
        while (!pre.empty()) {
            const auto dot = pre.find('.');
            const auto part = pre.substr(0, dot);
            if (part.size() > 1 && part.front() == '0' &&
                part.find_first_not_of("0123456789") == std::string_view::npos) return false;
            if (dot == std::string_view::npos) break;
            pre.remove_prefix(dot + 1);
        }
        return true;
    }
    static std::string compatibility(std::uint32_t route, std::string_view product) {
        if (!validProduct(product)) return {};
        const auto suffix = route == RP1_GPCLK_ROUTE_GPIO4 ? "-rp1-gpio4" :
            route == RP1_GPCLK_ROUTE_GPIO20 ? "-rp1-gpio20" : "";
        if (!*suffix) return {};
        return "v" + std::string(product) + suffix;
    }
};
} // namespace wsprrypi
