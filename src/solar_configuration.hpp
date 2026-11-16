// SPDX-License-Identifier: MIT
#pragma once
#include "solar_policy.hpp"
#include "json.hpp"
namespace wsprrypi {
// Exact current shared shape plus Pi-owned numeric/geometry semantics. Rejects
// unfinished solar activation even when loaded from a protected private record.
void validate_stored_solar(const nlohmann::json &);
SolarPolicyConfig parse_solar_configuration(const nlohmann::json &,const ArgParserConfig &,
    const nlohmann::json *previous=nullptr,bool validate_changes=false);
// Configuration-owner validation only; caller holds config_update_mutex.
// WTP uses current verified cached observations and performs no negotiation.
bool solar_backend_frequencies(const ArgParserConfig &,ModeType,std::span<const std::uint64_t>);
struct NativeSolarSnapshot {
    SolarPolicyConfig policy;
    ArgParserConfig ordinary;
    std::uint64_t generation{};
    std::string revision;
};
}
