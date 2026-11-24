// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <span>
namespace wsprrypi {
struct CdcDescriptorBinding {
    unsigned control_interface{},data_interface{};
    bool operator==(const CdcDescriptorBinding &) const = default;
};
// Resolve the actual CDC Union descriptor. Never infer roles from port names,
// adjacent interface numbers, enumeration order, or a product's usual layout.
CdcDescriptorBinding cdc_descriptor_binding(std::span<const std::uint8_t> configuration,
    unsigned selected_interface);
}
