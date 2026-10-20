#pragma once
#include <filesystem>
#include <string>
enum class SupportBundleResultFailure { none, missing, ambiguous, unsafe_file, oversized, malformed, invalid, inconsistent };
struct SupportBundleResultValidation {
    bool valid = false;
    SupportBundleResultFailure failure = SupportBundleResultFailure::invalid;
    std::string archive_filename, checksum_filename, sha256, i2c_probe_status;
    std::string case_id;
    bool manifest_included = false;
};
SupportBundleResultValidation validate_support_bundle_result(const std::filesystem::path &job_directory, bool expected_probe_i2c);
