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
// Pure target-owned authenticated-registration projection. The caller supplies
// a verified machine principal and a digest-bound committed Pair/1 receipt
// under its final mutation guard; this helper grants no authority or I/O.
// Existing rows and unknown completeness survive updates and manager changes.
nlohmann::json recurrence_authority_registration(const nlohmann::json &record,
    const nlohmann::json &request,const nlohmann::json &receipt,std::string_view history_id);
// Pure first-handover snapshot from verified complete target history. It keeps
// every recorded pair/version covered by each logical assignment, selects its
// original retirement credential scope and freezes the latest known digest.
// No key lookup, eligibility, persistence or dispatch is performed here.
nlohmann::json recurrence_retirement_plan(const nlohmann::json &record,std::string_view transition);
// Pure queries on an already validated owner record. Keep the latest proven
// consumed-grant lineage when a later refused grant did not change permission.
nlohmann::json recurrence_retirement_basis(const nlohmann::json &record);
bool recurrence_remote_resume_active(const nlohmann::json &record);
// Retain an exact already confirmed permanent fence, never relabel its proof.
// A consumed grant at/after that fence requires fresh retirement instead.
nlohmann::json recurrence_reusable_retirement(const nlohmann::json &record,const nlohmann::json &planned_request);
// Positive closure of one already revoked incoming management scope. Requires
// complete owner history and every recorded assignment permanently fenced;
// pending grants and consumed lineages without a fresher fence keep their key.
// This pure query never revokes access or authorizes deletion by itself.
bool recurrence_retirement_scope_closed(const nlohmann::json &,std::string_view pair,std::string_view generation);
// An unknown baseline, not a clean-install attestation. No persistence or I/O.
nlohmann::json unclassified_recurrence_authority(std::string_view self);
// Positive first-initialization proof, atomically published with its private
// directory before any output admission. It is consumed by the first authority
// transaction and is bound to the owning physical INI path.
nlohmann::json recurrence_bootstrap_record(std::string_view self,std::string_view source_binding,std::string_view installation);
nlohmann::json parse_recurrence_storage_record(std::string_view bytes,std::string_view self,std::string_view source_binding);
}
