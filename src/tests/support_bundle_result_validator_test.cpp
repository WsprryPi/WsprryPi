#include "support_bundle_result_validator.hpp"
#include "json.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
static constexpr char kName[] = "WsprryPi-support-test.tar.gz.result.json";
static nlohmann::json valid(bool probe = false, std::string status = "skipped_by_user", std::string digest = std::string(64, 'A')) {
    return {{"schema_version", 1}, {"status", "success"}, {"archive_filename", "WsprryPi-support-test.tar.gz"}, {"sha256_filename", "WsprryPi-support-test.tar.gz.sha256"}, {"sha256", digest}, {"generated_at_utc", "20260101T000000Z"}, {"configuration_files_included", true}, {"full_logs_included", false}, {"i2c_probe_requested", probe}, {"i2c_probe_status", status}, {"privileged_diagnostics_may_be_incomplete", false}};
}
static fs::path write(const fs::path &root, const nlohmann::json &json, const std::string &name = kName) { const auto path = root / name; std::ofstream(path) << json.dump(); return path; }
static void clear(const fs::path &root) { for (const auto &entry : fs::directory_iterator(root)) fs::remove_all(entry.path()); }
static void invalid(const fs::path &root, SupportBundleResultFailure expected, bool probe = false) { const auto result = validate_support_bundle_result(root, probe); assert(!result.valid && result.failure == expected); assert(result.archive_filename.empty() && result.checksum_filename.empty() && result.sha256.empty() && result.i2c_probe_status.empty() && result.case_id.empty() && !result.manifest_included); clear(root); }

int main() {
    char template_path[] = "/tmp/wsprrypi-validator.XXXXXX"; assert(mkdtemp(template_path)); const fs::path root = template_path;
    // Valid collector outcomes.
    write(root, valid()); auto result = validate_support_bundle_result(root, false); assert(result.valid && result.sha256 == std::string(64, 'a')); clear(root);
    auto private_result = valid(); private_result["case_id"] = "7K3M-9QFX-2DPA"; private_result["manifest_included"] = true;
    write(root, private_result); result = validate_support_bundle_result(root, false); assert(result.valid && result.case_id == "7K3M-9QFX-2DPA" && result.manifest_included); clear(root);
    auto legacy_extended = valid(); legacy_extended["case_id"] = nullptr; legacy_extended["manifest_included"] = false;
    write(root, legacy_extended); result = validate_support_bundle_result(root, false); assert(result.valid && result.case_id.empty() && !result.manifest_included); clear(root);
    for (const char *status : {"succeeded", "failed", "unavailable"}) { write(root, valid(true, status)); result = validate_support_bundle_result(root, true); assert(result.valid && result.i2c_probe_status == status); clear(root); }
    // Discovery and file safety.
    invalid(root, SupportBundleResultFailure::missing); write(root, valid()); write(root, valid(), "WsprryPi-support-two.tar.gz.result.json"); invalid(root, SupportBundleResultFailure::ambiguous);
    auto path = root / kName; assert(symlink("/tmp", path.c_str()) == 0); invalid(root, SupportBundleResultFailure::unsafe_file); fs::create_directory(path); invalid(root, SupportBundleResultFailure::unsafe_file); std::ofstream(path) << std::string(65537, 'x'); invalid(root, SupportBundleResultFailure::oversized);
    std::ofstream(path) << "{"; invalid(root, SupportBundleResultFailure::malformed); std::ofstream(path) << "[]"; invalid(root, SupportBundleResultFailure::invalid);
    // Required fields and types.
    for (const char *field : {"archive_filename", "sha256_filename", "sha256", "generated_at_utc", "i2c_probe_status"}) { auto json = valid(); json.erase(field); write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json[field] = true; write(root, json); invalid(root, SupportBundleResultFailure::invalid); }
    for (const char *field : {"configuration_files_included", "full_logs_included", "i2c_probe_requested", "privileged_diagnostics_may_be_incomplete"}) { auto json = valid(); json.erase(field); write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json[field] = "true"; write(root, json); invalid(root, SupportBundleResultFailure::invalid); }
    for (const char *field : {"schema_version", "status"}) { auto json = valid(); json.erase(field); write(root, json); invalid(root, SupportBundleResultFailure::invalid); }
    auto json = valid(); json["schema_version"] = "1"; write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json["schema_version"] = 2; write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json["status"] = true; write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json["status"] = "failure"; write(root, json); invalid(root, SupportBundleResultFailure::invalid);
    // Artifact names and digest.
    for (const std::string &value : std::vector<std::string>{"", "../bad", "bad/name", "bad\\name", std::string("bad\1", 4), "bad.tar.gz"}) { json = valid(); json["archive_filename"] = value; write(root, json); invalid(root, SupportBundleResultFailure::invalid); }
    json = valid(); json["archive_filename"] = "WsprryPi-support-other.tar.gz"; write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json["sha256_filename"] = "../bad"; write(root, json); invalid(root, SupportBundleResultFailure::invalid); json = valid(); json["sha256_filename"] = "wrong.sha256"; write(root, json); invalid(root, SupportBundleResultFailure::invalid);
    for (const std::string &value : {std::string(63, 'a'), std::string(65, 'a'), std::string(64, 'g'), std::string(62, 'a') + "\xC2\x80"}) { json = valid(false, "skipped_by_user", value); write(root, json); invalid(root, SupportBundleResultFailure::invalid); }
    // I2C consistency.
    for (const char *status : {"succeeded", "failed", "unavailable", "unknown"}) { write(root, valid(false, status)); invalid(root, SupportBundleResultFailure::inconsistent); }
    write(root, valid(true, "skipped_by_user")); invalid(root, SupportBundleResultFailure::inconsistent, true); write(root, valid(true, "succeeded")); invalid(root, SupportBundleResultFailure::inconsistent, false);
    // Private candidate metadata is all-or-nothing and internally consistent.
    json = valid(); json["case_id"] = "7K3M-9QFX-2DPA"; write(root, json); invalid(root, SupportBundleResultFailure::invalid);
    json = valid(); json["manifest_included"] = true; write(root, json); invalid(root, SupportBundleResultFailure::invalid);
    json = valid(); json["case_id"] = "bad"; json["manifest_included"] = true; write(root, json); invalid(root, SupportBundleResultFailure::inconsistent);
    json = valid(); json["case_id"] = "7K3M-9QFU-2DPA"; json["manifest_included"] = true; write(root, json); invalid(root, SupportBundleResultFailure::inconsistent);
    json = valid(); json["case_id"] = "7K3M-9QFX-2DPA"; json["manifest_included"] = false; write(root, json); invalid(root, SupportBundleResultFailure::inconsistent);
    json = valid(); json["case_id"] = nullptr; json["manifest_included"] = true; write(root, json); invalid(root, SupportBundleResultFailure::inconsistent);
    json = valid(); json["case_id"] = nullptr; json["manifest_included"] = "false"; write(root, json); invalid(root, SupportBundleResultFailure::invalid);
    fs::remove_all(root); std::cout << "support_bundle_result_validator_test: PASS\n";
}
