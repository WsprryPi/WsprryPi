/* SPDX-License-Identifier: MIT */
#pragma once
#include "rp1_gpclk_uapi.h"
#include <string_view>
#include <cstdint>

namespace wsprrypi {
// Product compatibility is independent of publication and physical qualification.
struct Rp1GpclkConsumerContract {
    static constexpr std::string_view module = "rp1-gpclk-dkms";
    static constexpr std::string_view version = "0.0.1";
    static constexpr std::string_view compatibility(
        std::uint32_t route, std::string_view product = version) noexcept {
        if (product != version) return "";
        return route == RP1_GPCLK_ROUTE_GPIO4 ? "v0.0.1-rp1-gpio4" :
            route == RP1_GPCLK_ROUTE_GPIO20 ? "v0.0.1-rp1-gpio20" : "";
    }
};
} // namespace wsprrypi
