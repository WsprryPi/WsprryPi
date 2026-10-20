#include "support_bundle_intake_runtime.hpp"

#include "version.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace {

constexpr std::size_t kMaximumSigningKeys = 16;
constexpr std::size_t kMaximumBundleKeys = 16;

bool ascii_digits(const std::string &value, std::size_t offset, std::size_t count) {
    if (offset + count > value.size()) return false;
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(offset),
                       value.begin() + static_cast<std::ptrdiff_t>(offset + count),
                       [](unsigned char character) {
                           return character >= '0' && character <= '9';
                       });
}

bool valid_key_id(const std::string &value, std::string_view purpose) {
    const std::string prefix = "wsprrypi-" + std::string(purpose) + "-";
    return value.size() == prefix.size() + 7 && value.starts_with(prefix) &&
           ascii_digits(value, prefix.size(), 4) &&
           value[prefix.size() + 4] == '-' &&
           ascii_digits(value, prefix.size() + 5, 2);
}

bool valid_trust(const SupportBundleIntakeRuntimeTrust &trust) {
    if (trust.signing_keys.empty() ||
        trust.signing_keys.size() > kMaximumSigningKeys ||
        trust.recognized_bundle_key_ids.empty() ||
        trust.recognized_bundle_key_ids.size() > kMaximumBundleKeys)
        return false;
    std::set<std::string> signing_ids;
    for (const auto &key : trust.signing_keys) {
        if (!valid_key_id(key.key_id, "intake") ||
            !signing_ids.insert(key.key_id).second ||
            std::all_of(key.public_key.begin(), key.public_key.end(),
                        [](unsigned char byte) { return byte == 0; }))
            return false;
    }
    std::set<std::string> bundle_ids;
    for (const auto &key_id : trust.recognized_bundle_key_ids) {
        if (!valid_key_id(key_id, "bundle") ||
            !bundle_ids.insert(key_id).second)
            return false;
    }
    return true;
}

SupportBundleIntakeRuntimeResult resolve_internal(
    const SupportBundleIntakeRuntimeTrust &trust,
    const SupportBundleIntakeRuntimeTestDependencies &dependencies) {
    try {
        if (!valid_trust(trust))
            return {SupportBundleIntakeRuntimeStatus::invalid_trust_metadata, {}};
        if (!dependencies.state_root.is_absolute() || !dependencies.installed_version ||
            !dependencies.now_utc_seconds || !dependencies.resolve)
            return {SupportBundleIntakeRuntimeStatus::invalid_runtime_environment, {}};
        const auto version = dependencies.installed_version();
        const auto now = dependencies.now_utc_seconds();
        if (!valid_support_bundle_semver(version) || !now || *now <= 0)
            return {SupportBundleIntakeRuntimeStatus::invalid_runtime_environment, {}};
        SupportBundleIntakeControllerRequest request;
        request.state_root = dependencies.state_root;
        request.signing_keys = trust.signing_keys;
        request.recognized_bundle_key_ids = trust.recognized_bundle_key_ids;
        request.installed_upload_version = version;
        request.now_utc_seconds = *now;
        request.client_protocol = kSupportBundleIntakeClientProtocol;
        return {SupportBundleIntakeRuntimeStatus::completed,
                dependencies.resolve(request)};
    } catch (...) {
        return {SupportBundleIntakeRuntimeStatus::resolution_failed, {}};
    }
}

SupportBundleIntakeRuntimeTestDependencies production_dependencies() {
    return {
        std::string(kSupportBundleIntakeProductionStateRoot),
        get_exe_version,
        []() -> std::optional<std::int64_t> {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::seconds>(now).count();
        },
        resolve_support_bundle_intake,
    };
}

} // namespace

SupportBundleIntakeRuntimeResult resolve_support_bundle_intake_runtime(
    const SupportBundleIntakeRuntimeTrust &trust) {
    return resolve_internal(trust, production_dependencies());
}

SupportBundleIntakeRuntimeResult resolve_support_bundle_intake_runtime_for_test(
    const SupportBundleIntakeRuntimeTrust &trust,
    const SupportBundleIntakeRuntimeTestDependencies &dependencies) {
    return resolve_internal(trust, dependencies);
}
