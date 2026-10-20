#include "support_bundle_result_validator.hpp"
#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;
namespace {
bool ascii_hex(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}
bool safe_basename(const std::string &value) {
    return !value.empty() && value.find('/') == value.npos && value.find('\\') == value.npos && value.find("..") == value.npos &&
        std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool result_name(const std::string &name) { return name.starts_with("WsprryPi-support-") && name.ends_with(".tar.gz.result.json"); }
bool valid_case_id(const std::string &value) {
    if (value.size() != 14 || value[4] != '-' || value[9] != '-') return false;
    constexpr std::string_view alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 9) continue;
        if (alphabet.find(value[index]) == alphabet.npos) return false;
    }
    return true;
}
}

SupportBundleResultValidation validate_support_bundle_result(const fs::path &directory, bool expected_probe) {
    SupportBundleResultValidation output;
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) { output.failure = SupportBundleResultFailure::missing; return output; }
    fs::path result_path;
    for (fs::directory_iterator iterator(directory, error), end; !error && iterator != end; iterator.increment(error)) {
        if (!result_name(iterator->path().filename().string())) continue;
        if (!result_path.empty()) { output.failure = SupportBundleResultFailure::ambiguous; return output; }
        result_path = iterator->path();
    }
    if (error || result_path.empty()) { output.failure = SupportBundleResultFailure::missing; return output; }
    if (fs::is_symlink(result_path, error) || error || !fs::is_regular_file(result_path, error) || error) { output.failure = SupportBundleResultFailure::unsafe_file; return output; }
    const auto size = fs::file_size(result_path, error);
    if (error) { output.failure = SupportBundleResultFailure::malformed; return output; }
    if (size > 65536) { output.failure = SupportBundleResultFailure::oversized; return output; }
    std::ifstream input(result_path);
    if (!input) { output.failure = SupportBundleResultFailure::malformed; return output; }
    nlohmann::json json;
    try { input >> json; } catch (...) { output.failure = SupportBundleResultFailure::malformed; return output; }
    if (!json.is_object() || !json.contains("schema_version") || !json["schema_version"].is_number_integer() || json["schema_version"] != 1 || !json.contains("status") || !json["status"].is_string() || json["status"] != "success") { output.failure = SupportBundleResultFailure::invalid; return output; }
    for (const char *key : {"archive_filename", "sha256_filename", "sha256", "generated_at_utc", "i2c_probe_status"}) if (!json.contains(key) || !json[key].is_string()) { output.failure = SupportBundleResultFailure::invalid; return output; }
    for (const char *key : {"configuration_files_included", "full_logs_included", "i2c_probe_requested", "privileged_diagnostics_may_be_incomplete"}) if (!json.contains(key) || !json[key].is_boolean()) { output.failure = SupportBundleResultFailure::invalid; return output; }
    const auto archive = json["archive_filename"].get<std::string>(), checksum = json["sha256_filename"].get<std::string>();
    auto digest = json["sha256"].get<std::string>(); const auto i2c = json["i2c_probe_status"].get<std::string>();
    const auto expected_archive = result_path.filename().string().substr(0, result_path.filename().string().size() - std::string(".result.json").size());
    if (!safe_basename(archive) || !safe_basename(checksum) || !ascii_hex(digest) || archive != expected_archive || !archive.starts_with("WsprryPi-support-") || !archive.ends_with(".tar.gz") || checksum != archive + ".sha256") { output.failure = SupportBundleResultFailure::invalid; return output; }
    if (json["i2c_probe_requested"].get<bool>() != expected_probe || (!expected_probe && i2c != "skipped_by_user") || (expected_probe && i2c != "succeeded" && i2c != "failed" && i2c != "unavailable")) { output.failure = SupportBundleResultFailure::inconsistent; return output; }
    const bool has_case_id = json.contains("case_id");
    const bool has_manifest = json.contains("manifest_included");
    std::string case_id;
    bool manifest_included = false;
    if (has_case_id || has_manifest) {
        if (!has_case_id || !has_manifest || !json["manifest_included"].is_boolean()) {
            output.failure = SupportBundleResultFailure::invalid; return output;
        }
        manifest_included = json["manifest_included"].get<bool>();
        if (json["case_id"].is_null()) {
            if (manifest_included) { output.failure = SupportBundleResultFailure::inconsistent; return output; }
        } else {
            if (!json["case_id"].is_string()) {
                output.failure = SupportBundleResultFailure::inconsistent; return output;
            }
            case_id = json["case_id"].get<std::string>();
            if (!valid_case_id(case_id) || !manifest_included) {
                output.failure = SupportBundleResultFailure::inconsistent; return output;
            }
        }
    }
    std::transform(digest.begin(), digest.end(), digest.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c); });
    return {true, SupportBundleResultFailure::none, archive, checksum, digest, i2c,
            case_id, manifest_included};
}
