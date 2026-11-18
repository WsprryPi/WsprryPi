// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string_view>
namespace wsprrypi {
enum class PiPairRoutePurpose {Bootstrap,Management,Retirement};
// Syntax/conformance only, never authority. Point curve validation, proof
// verification, current authorization, and durable admission belong to owners.
bool valid_pi_pair_document(const nlohmann::json &,std::string_view family);
nlohmann::json parse_pi_pair_document(std::string_view,std::string_view family);
bool pi_pair_route(PiPairRoutePurpose,std::string_view method,std::string_view resource);
} // namespace wsprrypi
