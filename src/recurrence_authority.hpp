// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <string>
#include <string_view>
namespace wsprrypi {
// Independent private pi-recurrence-authority/1 encoding. Structural validity
// is never proof of inventory completeness, retirement, or output permission.
bool valid_recurrence_authority_record(const nlohmann::json &,std::string_view self) noexcept;
nlohmann::json parse_recurrence_authority_record(std::string_view bytes,std::string_view self);
// An unknown baseline, not a clean-install attestation. No persistence or I/O.
nlohmann::json unclassified_recurrence_authority(std::string_view self);
// Positive first-initialization proof, atomically published with its private
// directory before any output admission. It is consumed by the first authority
// transaction and is bound to the owning physical INI path.
nlohmann::json recurrence_bootstrap_record(std::string_view self,std::string_view source_binding,std::string_view installation);
nlohmann::json parse_recurrence_storage_record(std::string_view bytes,std::string_view self,std::string_view source_binding);
}
