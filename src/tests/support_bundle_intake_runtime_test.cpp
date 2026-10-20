#include "support_bundle_intake_runtime.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Link seam for this isolated target; production links version.cpp.
std::string get_exe_version() { return "1.3.0"; }

namespace {

SupportBundleIntakeSigningKey signing_key(
    const std::string &id = "wsprrypi-intake-2099-99",
    unsigned char marker = 1) {
    SupportBundleIntakeSigningKey key;
    key.key_id = id;
    key.public_key.back() = marker;
    return key;
}

SupportBundleIntakeRuntimeTrust valid_trust() {
    return {{signing_key()}, {"wsprrypi-bundle-2099-99"}};
}

SupportBundleIntakeRuntimeTestDependencies dependencies_for(
    int &calls,
    SupportBundleIntakeControllerResult controller = {}) {
    return {
        "/private/runtime/support-bundles",
        [] { return std::string("1.3.0-alpha.2+build.7"); },
        [] { return std::optional<std::int64_t>(1786986000); },
        [&calls, controller](const SupportBundleIntakeControllerRequest &request) {
            ++calls;
            assert(request.state_root == "/private/runtime/support-bundles");
            assert(request.installed_upload_version == "1.3.0-alpha.2+build.7");
            assert(request.now_utc_seconds == 1786986000);
            assert(request.client_protocol == kSupportBundleIntakeClientProtocol);
            assert(request.signing_keys.size() == 1);
            assert(request.signing_keys[0].key_id == "wsprrypi-intake-2099-99");
            assert(request.signing_keys[0].public_key.back() == 1);
            assert(request.recognized_bundle_key_ids ==
                   std::vector<std::string>{"wsprrypi-bundle-2099-99"});
            assert(request.retrieval.manifest_url == kWsprryPiIntakeManifestUrl);
            assert(request.retrieval.signature_url == kWsprryPiIntakeSignatureUrl);
            assert(request.retrieval.curl_executable == "/usr/bin/curl");
            assert(request.retrieval.connect_timeout == std::chrono::seconds(5));
            assert(request.retrieval.operation_timeout == std::chrono::seconds(15));
            assert(request.retrieval.maximum_manifest_bytes == 16 * 1024);
            assert(request.retrieval.maximum_signature_bytes == 2 * 1024);
            return controller;
        },
    };
}

void assert_not_invoked(const SupportBundleIntakeRuntimeTrust &trust,
                        SupportBundleIntakeRuntimeTestDependencies dependencies,
                        SupportBundleIntakeRuntimeStatus status) {
    int calls = 0;
    dependencies.resolve = [&](const SupportBundleIntakeControllerRequest &) {
        ++calls;
        return SupportBundleIntakeControllerResult{};
    };
    const auto result = resolve_support_bundle_intake_runtime_for_test(trust, dependencies);
    assert(result.status == status && !result.completed() && calls == 0);
    assert(!result.controller.ready() && !result.controller.upgrade);
}

void test_fixed_contract_and_request_construction() {
    assert(fs::path(kSupportBundleIntakeProductionStateRoot).is_absolute());
    assert(kSupportBundleIntakeProductionStateRoot ==
           std::string_view("/var/lib/wsprrypi/support-bundles"));
    assert(kSupportBundleIntakeClientProtocol == 1);
    int calls = 0;
    SupportBundleIntakeControllerResult controller;
    controller.failure = SupportBundleIntakeControllerFailure::none;
    controller.manifest.generation = 7;
    const auto result = resolve_support_bundle_intake_runtime_for_test(
        valid_trust(), dependencies_for(calls, controller));
    assert(result.completed() && calls == 1);
    assert(result.controller.ready() && result.controller.manifest.generation == 7);
}

void test_trust_preflight() {
    int ignored = 0;
    const auto dependencies = dependencies_for(ignored);
    for (auto trust : {
             SupportBundleIntakeRuntimeTrust{},
             SupportBundleIntakeRuntimeTrust{{signing_key()}, {}},
             SupportBundleIntakeRuntimeTrust{{}, {"wsprrypi-bundle-2099-99"}},
             SupportBundleIntakeRuntimeTrust{{signing_key("bad")},
                                             {"wsprrypi-bundle-2099-99"}},
             SupportBundleIntakeRuntimeTrust{{signing_key()}, {"bad"}},
             SupportBundleIntakeRuntimeTrust{{signing_key(), signing_key()},
                                             {"wsprrypi-bundle-2099-99"}},
             SupportBundleIntakeRuntimeTrust{{signing_key()},
                                             {"wsprrypi-bundle-2099-99",
                                              "wsprrypi-bundle-2099-99"}}}) {
        assert_not_invoked(trust, dependencies,
                           SupportBundleIntakeRuntimeStatus::invalid_trust_metadata);
    }
    auto zero = valid_trust();
    zero.signing_keys[0].public_key.fill(0);
    assert_not_invoked(zero, dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_trust_metadata);

    auto too_many_signing = valid_trust();
    too_many_signing.signing_keys.clear();
    for (int index = 0; index < 17; ++index) {
        auto key = signing_key("wsprrypi-intake-2099-" +
                               std::string(index < 10 ? "0" : "") +
                               std::to_string(index),
                               static_cast<unsigned char>(index + 1));
        too_many_signing.signing_keys.push_back(key);
    }
    assert_not_invoked(too_many_signing, dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_trust_metadata);

    auto too_many_bundle = valid_trust();
    too_many_bundle.recognized_bundle_key_ids.clear();
    for (int index = 0; index < 17; ++index) {
        too_many_bundle.recognized_bundle_key_ids.push_back(
            "wsprrypi-bundle-2099-" + std::string(index < 10 ? "0" : "") +
            std::to_string(index));
    }
    assert_not_invoked(too_many_bundle, dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_trust_metadata);
}

void test_runtime_environment_preflight_and_exceptions() {
    int calls = 0;
    auto dependencies = dependencies_for(calls);
    dependencies.state_root = "relative";
    assert_not_invoked(valid_trust(), dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);
    dependencies = dependencies_for(calls);
    dependencies.installed_version = [] { return std::string("unknown"); };
    assert_not_invoked(valid_trust(), dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);
    dependencies = dependencies_for(calls);
    dependencies.now_utc_seconds = [] { return std::optional<std::int64_t>{}; };
    assert_not_invoked(valid_trust(), dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);
    dependencies = dependencies_for(calls);
    dependencies.now_utc_seconds = [] { return std::optional<std::int64_t>(0); };
    assert_not_invoked(valid_trust(), dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);
    dependencies = dependencies_for(calls);
    dependencies.installed_version = {};
    assert_not_invoked(valid_trust(), dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);
    dependencies = dependencies_for(calls);
    dependencies.now_utc_seconds = {};
    assert_not_invoked(valid_trust(), dependencies,
                       SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);
    dependencies = dependencies_for(calls);
    dependencies.resolve = {};
    const auto missing = resolve_support_bundle_intake_runtime_for_test(
        valid_trust(), dependencies);
    assert(missing.status == SupportBundleIntakeRuntimeStatus::invalid_runtime_environment);

    dependencies = dependencies_for(calls);
    dependencies.installed_version = []() -> std::string {
        throw std::runtime_error("test");
    };
    auto result = resolve_support_bundle_intake_runtime_for_test(valid_trust(), dependencies);
    assert(result.status == SupportBundleIntakeRuntimeStatus::resolution_failed);
    dependencies = dependencies_for(calls);
    dependencies.resolve = [](const SupportBundleIntakeControllerRequest &)
        -> SupportBundleIntakeControllerResult { throw std::runtime_error("test"); };
    result = resolve_support_bundle_intake_runtime_for_test(valid_trust(), dependencies);
    assert(result.status == SupportBundleIntakeRuntimeStatus::resolution_failed);
    dependencies = dependencies_for(calls);
    dependencies.now_utc_seconds = []() -> std::optional<std::int64_t> {
        throw std::runtime_error("test");
    };
    result = resolve_support_bundle_intake_runtime_for_test(valid_trust(), dependencies);
    assert(result.status == SupportBundleIntakeRuntimeStatus::resolution_failed);
}

void test_controller_result_propagation() {
    const std::vector<SupportBundleIntakeControllerFailure> failures = {
        SupportBundleIntakeControllerFailure::none,
        SupportBundleIntakeControllerFailure::state_load_failed,
        SupportBundleIntakeControllerFailure::retrieval_failed,
        SupportBundleIntakeControllerFailure::validation_failed,
        SupportBundleIntakeControllerFailure::state_commit_failed,
        SupportBundleIntakeControllerFailure::state_durability_uncertain,
    };
    for (const auto failure : failures) {
        int calls = 0;
        SupportBundleIntakeControllerResult controller;
        controller.failure = failure;
        controller.retrieval_failure = SupportBundleIntakeRetrievalFailure::signature_timeout;
        controller.validation_failure = SupportBundleIntakeFailure::upgrade_required;
        controller.state_commit_status =
            SupportBundleIntakeStateCommitStatus::committed_sync_uncertain;
        if (failure == SupportBundleIntakeControllerFailure::none) {
            controller.manifest.status = "disabled";
        } else if (failure == SupportBundleIntakeControllerFailure::validation_failed) {
            controller.upgrade = SupportBundleIntakeValidationResult::UpgradeRequirement{
                "9.0.0", "https://github.com/WsprryPi/WsprryPi/releases/latest", std::nullopt};
        }
        const auto result = resolve_support_bundle_intake_runtime_for_test(
            valid_trust(), dependencies_for(calls, controller));
        assert(result.completed() && calls == 1);
        assert(result.controller.failure == failure);
        assert(result.controller.retrieval_failure == controller.retrieval_failure);
        assert(result.controller.validation_failure == controller.validation_failure);
        assert(result.controller.state_commit_status == controller.state_commit_status);
        assert(result.controller.manifest.status == controller.manifest.status);
        assert(result.controller.upgrade.has_value() == controller.upgrade.has_value());
    }
}

} // namespace

int main() {
    test_fixed_contract_and_request_construction();
    test_trust_preflight();
    test_runtime_environment_preflight_and_exceptions();
    test_controller_result_propagation();
    std::cout << "support_bundle_intake_runtime_test: PASS\n";
}
