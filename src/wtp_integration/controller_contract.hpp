// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include "network_http.hpp"
#include <string_view>
namespace wsprrypi {
// Closed controller documents from the authoritative Pico Control/1 owner.
// Parsing/duplicate/header/origin checks precede this shape validator.
bool valid_controller_document(const nlohmann::json &,std::string_view name);
// Structural validation only; the Pi's owning configuration and scheduling
// policies remain mandatory before a transaction can reserve or persist.
bool valid_pi_candidate_shape(const nlohmann::json &);
bool valid_pi_configuration_field(std::string_view section,std::string_view key,const nlohmann::json &);
// Exact compiled Pi type identity, including the incorporated value policies.
bool valid_pi_resource_capability(const nlohmann::json &);
// Exact current Pico diagnostics, including its Control/1 envelope. This is
// observation data; enabled/requested_enabled describe distinct states.
bool valid_pico_network_observation(const nlohmann::json &);
// Target-owned Pi hashing: normalize exact integral floats and negative zero,
// sort object keys, preserve arrays/UTF-8, and emit lowercase hex controls.
std::string canonical_pi_management_json(const nlohmann::json &);
// Native success documents remain unchanged. Controller refusals expose only
// the approved closed error envelope and finite public error vocabulary.
PicoHttpResponse controller_target_error(unsigned status,std::string_view code);
PicoHttpResponse normalize_controller_error(const PicoHttpResponse &);
}
