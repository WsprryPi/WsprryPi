// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
namespace wsprrypi {
// Pure record encoding/validation for the approved private Pi formats. These
// helpers grant no authority and do not replace complete configuration policy,
// revision/ownership checks, WAL coordination, or admission inhibition.
// Read-only recognition of the exact existing protected storage shape.
// It is not accepted by current control requests or resource descriptors.
bool valid_pi_stored_candidate_shape(const nlohmann::json &) noexcept;
nlohmann::json current_pi_configuration(nlohmann::json);
nlohmann::json current_pi_settings_record(nlohmann::json);
struct PiOperationsRecord {nlohmann::json document;bool evicted{};};
nlohmann::json pi_managed_settings_record(const std::string &self,
    const std::string &generation,const nlohmann::json &candidate,
    const std::string &private_state_digest);
bool valid_pi_managed_settings_record(const nlohmann::json &,const std::string &self,
    const std::string &private_state_digest) noexcept;
bool valid_pi_operations_record(const nlohmann::json &,const std::string &self) noexcept;
// Private-file decoding has its own 1 MiB bound; it must not reuse the 32 KiB
// HTTP body limit or silently accept duplicate JSON keys before validation.
nlohmann::json parse_pi_managed_settings_record(const std::string &bytes,
    const std::string &self,const std::string &private_state_digest);
nlohmann::json parse_pi_operations_record(const std::string &bytes,const std::string &self);
// Accepts the previous bounded history plus one proposed entry. Retains every
// unresolved entry and the newest 256 terminal entries. The owner must retire
// its edit context when evicted is true. Input is never mutated on failure.
PiOperationsRecord pi_operations_record(const std::string &self,
    const nlohmann::json &receipts);
std::string pi_managed_request_digest(const std::string &self,
    const std::string &principal,const std::string &authorization_generation,
    const std::string &route,const nlohmann::json &request);
} // namespace wsprrypi
