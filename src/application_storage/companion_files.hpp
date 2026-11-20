// SPDX-License-Identifier: MIT
#pragma once
#include "json.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace wsprrypi {
enum class CompanionKind {FleetAssignments,FleetContexts,UdpLogging,UsbLogging,TargetManagement};
struct CompanionFile {
    CompanionKind kind;
    std::string target;
    std::uint64_t size{};
    std::string digest,signature;
    bool operator==(const CompanionFile &) const = default;
};
// Closed names and existing owner's bounds. No caller-supplied relative paths.
std::string companion_relative_path(const CompanionFile &);
std::string companion_source_path(const std::string &configuration_namespace,const CompanionFile &);
std::string companion_backup_name(const CompanionFile &);
std::uint64_t companion_maximum_bytes(CompanionKind);
std::optional<CompanionFile> inspect_companion_file(const std::string &configuration_namespace,CompanionKind,const std::string &target = {});
nlohmann::json companion_manifest(const std::vector<CompanionFile> &);
std::vector<CompanionFile> parse_companion_manifest(const nlohmann::json &);
// Streaming copy under the application's exclusive initialization/maintenance
// admission. The source observation must still match; destinations are private.
void copy_companion_file(const std::string &source,const std::string &destination,const CompanionFile &,bool original_source);
void verify_companion_file(const std::string &path,const CompanionFile &);
std::string read_logging_companion(const std::string &configuration_namespace,const CompanionFile &);
void prepare_companion_directory(const std::string &path);
} // namespace wsprrypi
