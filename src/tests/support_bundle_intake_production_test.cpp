#include "support_bundle_intake_production.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

std::string get_exe_version() { return "3.2.0"; }
bool valid_support_bundle_semver(const std::string &) noexcept { return true; }

SupportBundleIntakeControllerResult resolve_support_bundle_intake(
    const SupportBundleIntakeControllerRequest &) {
    return {};
}

namespace {

constexpr const char *kRequestUrl =
    "https://www.dropbox.com/request/TestOpaque_123";
constexpr const char *kReleaseUrl =
    "https://github.com/WsprryPi/WsprryPi/releases/latest";

SupportBundleIntakeRuntimeResult active_runtime() {
    SupportBundleIntakeRuntimeResult runtime;
    runtime.status = SupportBundleIntakeRuntimeStatus::completed;
    auto &controller = runtime.controller;
    controller.failure = SupportBundleIntakeControllerFailure::none;
    controller.state_load_status = SupportBundleIntakeStateLoadStatus::absent;
    controller.retrieval_failure = SupportBundleIntakeRetrievalFailure::none;
    controller.state_commit_status = SupportBundleIntakeStateCommitStatus::committed;
    controller.validation_failure = SupportBundleIntakeFailure::none;
    auto &manifest = controller.manifest;
    manifest.generation = 7;
    manifest.published_at = "2099-01-01T00:00:00Z";
    manifest.expires_at = "2099-04-01T00:00:00Z";
    manifest.status = "active";
    manifest.minimum_client_protocol = 1;
    manifest.minimum_upload_version = "3.2.0";
    manifest.request_url = kRequestUrl;
    manifest.release_url = kReleaseUrl;
    manifest.user_message = "Signed guidance.";
    manifest.bundle_encryption_key_id = "wsprrypi-bundle-2099-01";
    manifest.manifest_sha256 = std::string(64, 'a');
    manifest.signing_key_id = "wsprrypi-intake-2099-01";
    return runtime;
}

void assert_empty(const SupportBundleIntakeProductionResult &result) {
    assert(result.status == SupportBundleIntakeProductionStatus::unavailable);
    assert(result.generation == 0 && result.expires_at.empty());
    assert(result.minimum_upload_version.empty());
    assert(result.signing_key_id.empty() && result.bundle_key_id.empty());
    assert(!result.request_url && result.release_url.empty());
    assert(!result.user_message);
}

SupportBundleIntakeProductionResult resolve(
    const SupportBundleIntakeRuntimeResult &runtime, int &calls) {
    return resolve_support_bundle_intake_production_for_test([&] {
        ++calls;
        return runtime;
    });
}

void test_active_is_exact_and_provider_called_once() {
    int calls = 0;
    const auto result = resolve(active_runtime(), calls);
    assert(calls == 1 && result.status == SupportBundleIntakeProductionStatus::active);
    assert(result.generation == 7 && result.expires_at == "2099-04-01T00:00:00Z");
    assert(result.minimum_upload_version == "3.2.0");
    assert(result.signing_key_id == "wsprrypi-intake-2099-01");
    assert(result.bundle_key_id == "wsprrypi-bundle-2099-01");
    assert(result.request_url == kRequestUrl && result.release_url.empty());
    assert(result.user_message == "Signed guidance.");
}

void test_disabled_withholds_capability_and_upgrade_fields() {
    auto runtime = active_runtime();
    runtime.controller.manifest.status = "disabled";
    runtime.controller.manifest.request_url.reset();
    int calls = 0;
    const auto result = resolve(runtime, calls);
    assert(calls == 1 && result.status == SupportBundleIntakeProductionStatus::disabled);
    assert(result.generation == 7 && !result.request_url);
    assert(result.minimum_upload_version.empty() && result.release_url.empty());
    assert(result.signing_key_id == "wsprrypi-intake-2099-01");
    assert(result.bundle_key_id == "wsprrypi-bundle-2099-01");
    assert(result.user_message == "Signed guidance.");
}

void test_upgrade_is_limited_and_durable() {
    SupportBundleIntakeRuntimeResult runtime;
    runtime.status = SupportBundleIntakeRuntimeStatus::completed;
    runtime.controller.failure = SupportBundleIntakeControllerFailure::validation_failed;
    runtime.controller.state_load_status = SupportBundleIntakeStateLoadStatus::loaded;
    runtime.controller.retrieval_failure = SupportBundleIntakeRetrievalFailure::none;
    runtime.controller.validation_failure = SupportBundleIntakeFailure::upgrade_required;
    runtime.controller.state_commit_status = SupportBundleIntakeStateCommitStatus::unchanged;
    runtime.controller.upgrade = SupportBundleIntakeValidationResult::UpgradeRequirement{
        "9.0.0", kReleaseUrl, "Upgrade before upload."};
    int calls = 0;
    const auto result = resolve(runtime, calls);
    assert(calls == 1 &&
           result.status == SupportBundleIntakeProductionStatus::upgrade_required);
    assert(result.minimum_upload_version == "9.0.0");
    assert(result.release_url == kReleaseUrl);
    assert(result.user_message == "Upgrade before upload.");
    assert(result.generation == 0 && !result.request_url);
    assert(result.expires_at.empty() && result.signing_key_id.empty() &&
           result.bundle_key_id.empty());

    for (const bool clear_minimum : {true, false}) {
        auto malformed = runtime;
        if (clear_minimum) {
            malformed.controller.upgrade->minimum_upload_version.clear();
        } else {
            malformed.controller.upgrade->release_url.clear();
        }
        int malformed_calls = 0;
        assert_empty(resolve(malformed, malformed_calls));
        assert(malformed_calls == 1);
    }
}

void test_malformed_successes_fail_closed() {
    std::vector<SupportBundleIntakeRuntimeResult> malformed;
    auto value = active_runtime(); value.controller.state_commit_status =
        SupportBundleIntakeStateCommitStatus::committed_sync_uncertain; malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.request_url.reset(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.generation = 0; malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.published_at.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.expires_at.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.minimum_client_protocol = 0; malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.minimum_upload_version.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.release_url.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.signing_key_id.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.bundle_encryption_key_id.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.manifest_sha256.clear(); malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.status = "unknown"; malformed.push_back(value);
    value = active_runtime(); value.controller.upgrade =
        SupportBundleIntakeValidationResult::UpgradeRequirement{"9.0.0", kReleaseUrl, {}};
    malformed.push_back(value);
    value = active_runtime(); value.controller.manifest.status = "disabled"; malformed.push_back(value);
    value = active_runtime(); value.controller.state_load_status =
        SupportBundleIntakeStateLoadStatus::invalid_state; malformed.push_back(value);
    value = active_runtime(); value.controller.retrieval_failure =
        SupportBundleIntakeRetrievalFailure::manifest_failed; malformed.push_back(value);
    value = active_runtime(); value.controller.validation_failure =
        SupportBundleIntakeFailure::invalid_manifest; malformed.push_back(value);
    for (const auto &runtime : malformed) {
        int calls = 0;
        assert_empty(resolve(runtime, calls));
        assert(calls == 1);
    }
}

void test_runtime_controller_and_upgrade_failures_erase_fields() {
    for (const auto status : {SupportBundleIntakeRuntimeStatus::invalid_trust_metadata,
                              SupportBundleIntakeRuntimeStatus::invalid_runtime_environment,
                              SupportBundleIntakeRuntimeStatus::resolution_failed}) {
        auto runtime = active_runtime(); runtime.status = status;
        int calls = 0; assert_empty(resolve(runtime, calls)); assert(calls == 1);
    }
    for (const auto failure : {SupportBundleIntakeControllerFailure::state_load_failed,
                               SupportBundleIntakeControllerFailure::retrieval_failed,
                               SupportBundleIntakeControllerFailure::validation_failed,
                               SupportBundleIntakeControllerFailure::state_commit_failed,
                               SupportBundleIntakeControllerFailure::state_durability_uncertain}) {
        auto runtime = active_runtime(); runtime.controller.failure = failure;
        int calls = 0; assert_empty(resolve(runtime, calls)); assert(calls == 1);
    }
    auto upgrade = active_runtime();
    upgrade.controller.failure = SupportBundleIntakeControllerFailure::validation_failed;
    upgrade.controller.validation_failure = SupportBundleIntakeFailure::upgrade_required;
    upgrade.controller.upgrade = SupportBundleIntakeValidationResult::UpgradeRequirement{
        "9.0.0", kReleaseUrl, {}};
    for (const auto commit : {SupportBundleIntakeStateCommitStatus::invalid_input,
                              SupportBundleIntakeStateCommitStatus::committed_sync_uncertain}) {
        upgrade.controller.state_commit_status = commit;
        int calls = 0; assert_empty(resolve(upgrade, calls)); assert(calls == 1);
    }
    upgrade.controller.state_commit_status = SupportBundleIntakeStateCommitStatus::committed;
    upgrade.controller.manifest.request_url = kRequestUrl;
    int calls = 0; assert_empty(resolve(upgrade, calls)); assert(calls == 1);
}

void test_missing_provider_and_exception_fail_closed() {
    assert_empty(resolve_support_bundle_intake_production_for_test({}));
    assert_empty(resolve_support_bundle_intake_production_for_test([]()
        -> SupportBundleIntakeRuntimeResult { throw std::runtime_error("secret"); }));
}

} // namespace

int main() {
    test_active_is_exact_and_provider_called_once();
    test_disabled_withholds_capability_and_upgrade_fields();
    test_upgrade_is_limited_and_durable();
    test_malformed_successes_fail_closed();
    test_runtime_controller_and_upgrade_failures_erase_fields();
    test_missing_provider_and_exception_fail_closed();
    std::cout << "support_bundle_intake_production_test: PASS\n";
}
