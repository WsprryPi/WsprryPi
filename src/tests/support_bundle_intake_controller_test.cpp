#include "support_bundle_intake_controller.hpp"
#include "json.hpp"

#include <openssl/evp.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char *kDigestA =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char *kDigestB =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using KeygenContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

struct TemporaryRoot {
    fs::path path;
    TemporaryRoot() {
        std::string pattern = (fs::temp_directory_path() / "wsprrypi-intake-controller-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const auto created = mkdtemp(buffer.data());
        assert(created);
        path = created;
        assert(chmod(path.c_str(), 0700) == 0);
    }
    ~TemporaryRoot() { fs::remove_all(path); }
};

struct SigningFixture {
    Key key{nullptr, EVP_PKEY_free};
    SupportBundleIntakeSigningKey pinned;
};

SigningFixture make_signing_fixture() {
    KeygenContext context(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), EVP_PKEY_CTX_free);
    assert(context && EVP_PKEY_keygen_init(context.get()) == 1);
    EVP_PKEY *raw = nullptr;
    assert(EVP_PKEY_keygen(context.get(), &raw) == 1);
    SigningFixture fixture;
    fixture.key.reset(raw);
    fixture.pinned.key_id = "wsprrypi-intake-2099-99";
    std::size_t size = fixture.pinned.public_key.size();
    assert(EVP_PKEY_get_raw_public_key(
               fixture.key.get(), fixture.pinned.public_key.data(), &size) == 1);
    assert(size == fixture.pinned.public_key.size());
    return fixture;
}

std::string base64url(const std::vector<unsigned char> &bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded;
    std::uint32_t accumulator = 0;
    unsigned bits = 0;
    for (const auto byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            encoded.push_back(alphabet[(accumulator >> bits) & 63]);
        }
    }
    if (bits) encoded.push_back(alphabet[(accumulator << (6 - bits)) & 63]);
    return encoded;
}

std::string signed_envelope(EVP_PKEY *key, const std::string &manifest) {
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    assert(context && EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key) == 1);
    std::size_t size = 0;
    assert(EVP_DigestSign(context.get(), nullptr, &size,
                          reinterpret_cast<const unsigned char *>(manifest.data()),
                          manifest.size()) == 1);
    std::vector<unsigned char> signature(size);
    assert(EVP_DigestSign(context.get(), signature.data(), &size,
                          reinterpret_cast<const unsigned char *>(manifest.data()),
                          manifest.size()) == 1);
    signature.resize(size);
    return json({{"schema_version", 1}, {"algorithm", "Ed25519"},
                 {"key_id", "wsprrypi-intake-2099-99"},
                 {"signature", base64url(signature)}}).dump();
}

SupportBundleIntakeManifest fake_manifest(std::uint64_t generation = 7,
                                          const std::string &digest = kDigestA) {
    SupportBundleIntakeManifest manifest;
    manifest.generation = generation;
    manifest.status = "active";
    manifest.request_url = "https://www.dropbox.com/request/TestOpaque_123";
    manifest.release_url = "https://github.com/WsprryPi/WsprryPi/releases/latest";
    manifest.bundle_encryption_key_id = "wsprrypi-bundle-2099-99";
    manifest.manifest_sha256 = digest;
    manifest.signing_key_id = "wsprrypi-intake-2099-99";
    return manifest;
}

SupportBundleIntakeControllerRequest basic_request() {
    SupportBundleIntakeControllerRequest request;
    request.state_root = "/private/test-state";
    request.recognized_bundle_key_ids = {"wsprrypi-bundle-2099-99"};
    request.installed_upload_version = "1.3.0";
    request.now_utc_seconds = 1786986000;
    return request;
}

void assert_no_manifest(const SupportBundleIntakeControllerResult &result) {
    assert(!result.ready());
    assert(result.manifest.generation == 0);
    assert(result.manifest.status.empty() && !result.manifest.request_url);
    assert(result.manifest.release_url.empty() && !result.manifest.user_message);
    assert(result.manifest.bundle_encryption_key_id.empty());
    assert(result.manifest.manifest_sha256.empty() && result.manifest.signing_key_id.empty());
}

SupportBundleIntakeControllerDependencies successful_dependencies(
    std::vector<std::string> &calls,
    SupportBundleIntakeStateLoadResult loaded =
        {SupportBundleIntakeStateLoadStatus::absent, {}}) {
    return {
        [&](const fs::path &root) {
            calls.push_back("load");
            assert(root == "/private/test-state");
            return loaded;
        },
        [&](const SupportBundleIntakeRetrievalRequest &) {
            calls.push_back("retrieve");
            return SupportBundleIntakeRetrievalResult{
                SupportBundleIntakeRetrievalFailure::none, "exact-manifest", "exact-signature"};
        },
        [&](const SupportBundleIntakeValidationRequest &request) {
            calls.push_back("validate");
            assert(request.manifest_bytes == "exact-manifest");
            assert(request.signature_envelope_bytes == "exact-signature");
            assert(request.now_utc_seconds == 1786986000 && request.client_protocol == 1);
            assert(request.installed_upload_version == "1.3.0");
            assert(request.recognized_bundle_key_ids ==
                   std::vector<std::string>{"wsprrypi-bundle-2099-99"});
            if (loaded.loaded()) {
                assert(request.previous && request.previous->generation == loaded.state.generation);
                assert(request.previous->manifest_sha256 == loaded.state.manifest_sha256);
            } else {
                assert(!request.previous);
            }
            return SupportBundleIntakeValidationResult{
                SupportBundleIntakeFailure::none, fake_manifest()};
        },
        [&](const fs::path &root, const SupportBundleIntakeState &state) {
            calls.push_back("commit");
            assert(root == "/private/test-state");
            assert(state.generation == 7 && state.manifest_sha256 == kDigestA);
            return SupportBundleIntakeStateCommitResult{
                SupportBundleIntakeStateCommitStatus::committed};
        },
    };
}

void test_order_absent_loaded_and_success() {
    std::vector<std::string> calls;
    auto dependencies = successful_dependencies(calls);
    auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
    assert(result.ready() && result.manifest.generation == 7);
    assert(result.state_load_status == SupportBundleIntakeStateLoadStatus::absent);
    assert(result.state_commit_status == SupportBundleIntakeStateCommitStatus::committed);
    assert(calls == std::vector<std::string>({"load", "retrieve", "validate", "commit"}));

    calls.clear();
    dependencies = successful_dependencies(
        calls, {SupportBundleIntakeStateLoadStatus::loaded, {6, kDigestB}});
    result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
    assert(result.ready());
    assert(calls == std::vector<std::string>({"load", "retrieve", "validate", "commit"}));
}

void test_load_failures_stop_before_retrieval() {
    for (const auto status : {
             SupportBundleIntakeStateLoadStatus::unsafe_root,
             SupportBundleIntakeStateLoadStatus::unsafe_state,
             SupportBundleIntakeStateLoadStatus::read_failed,
             SupportBundleIntakeStateLoadStatus::invalid_state}) {
        std::vector<std::string> calls;
        auto dependencies = successful_dependencies(calls);
        dependencies.load_state = [&](const fs::path &) {
            calls.push_back("load");
            return SupportBundleIntakeStateLoadResult{status, {}};
        };
        const auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
        assert(result.failure == SupportBundleIntakeControllerFailure::state_load_failed);
        assert(result.state_load_status == status && calls == std::vector<std::string>{"load"});
        assert_no_manifest(result);
        assert(!result.upgrade);
    }
}

void test_retrieval_and_validation_failures_stop_later_stages() {
    for (const auto failure : {
             SupportBundleIntakeRetrievalFailure::invalid_request,
             SupportBundleIntakeRetrievalFailure::executable_unavailable,
             SupportBundleIntakeRetrievalFailure::launch_failed,
             SupportBundleIntakeRetrievalFailure::manifest_timeout,
             SupportBundleIntakeRetrievalFailure::manifest_failed,
             SupportBundleIntakeRetrievalFailure::manifest_empty,
             SupportBundleIntakeRetrievalFailure::manifest_oversized,
             SupportBundleIntakeRetrievalFailure::signature_timeout,
             SupportBundleIntakeRetrievalFailure::signature_failed,
             SupportBundleIntakeRetrievalFailure::signature_empty,
             SupportBundleIntakeRetrievalFailure::signature_oversized}) {
        std::vector<std::string> calls;
        auto dependencies = successful_dependencies(calls);
        dependencies.retrieve = [&](const SupportBundleIntakeRetrievalRequest &) {
            calls.push_back("retrieve");
            return SupportBundleIntakeRetrievalResult{failure, "secret", "secret"};
        };
        const auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
        assert(result.failure == SupportBundleIntakeControllerFailure::retrieval_failed);
        assert(result.retrieval_failure == failure);
        assert(calls == std::vector<std::string>({"load", "retrieve"}));
        assert_no_manifest(result);
        assert(!result.upgrade);
    }

    for (const auto failure : {
             SupportBundleIntakeFailure::invalid_signature,
             SupportBundleIntakeFailure::invalid_manifest,
             SupportBundleIntakeFailure::rollback,
             SupportBundleIntakeFailure::same_generation_mutated,
             SupportBundleIntakeFailure::outside_validity_window,
             SupportBundleIntakeFailure::incompatible_client}) {
        std::vector<std::string> calls;
        auto dependencies = successful_dependencies(calls);
        dependencies.validate = [&](const SupportBundleIntakeValidationRequest &) {
            calls.push_back("validate");
            return SupportBundleIntakeValidationResult{failure, fake_manifest()};
        };
        const auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
        assert(result.failure == SupportBundleIntakeControllerFailure::validation_failed);
        assert(result.validation_failure == failure);
        assert(calls == std::vector<std::string>({"load", "retrieve", "validate"}));
        assert_no_manifest(result);
        assert(!result.upgrade);
    }
}

void test_upgrade_requirement_is_limited_and_committed_before_disclosure() {
    std::vector<std::string> calls;
    auto dependencies = successful_dependencies(calls);
    dependencies.validate = [&](const SupportBundleIntakeValidationRequest &) {
        calls.push_back("validate");
        auto manifest = fake_manifest();
        manifest.request_url = "https://www.dropbox.com/request/NEVER_DISCLOSE_THIS";
        SupportBundleIntakeValidationResult result;
        result.failure = SupportBundleIntakeFailure::upgrade_required;
        result.manifest = manifest;
        result.upgrade = SupportBundleIntakeValidationResult::UpgradeRequirement{
            "9.0.0", "https://github.com/WsprryPi/WsprryPi/releases/latest",
            "Upgrade before uploading."};
        result.accepted_state = SupportBundleIntakePreviousState{7, kDigestA};
        return result;
    };
    const auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
    assert(result.failure == SupportBundleIntakeControllerFailure::validation_failed);
    assert(result.validation_failure == SupportBundleIntakeFailure::upgrade_required);
    assert(calls == std::vector<std::string>({"load", "retrieve", "validate", "commit"}));
    assert_no_manifest(result);
    assert(result.upgrade && result.upgrade->minimum_upload_version == "9.0.0");
    assert(result.upgrade->release_url ==
           "https://github.com/WsprryPi/WsprryPi/releases/latest");
    assert(result.upgrade->user_message == "Upgrade before uploading.");
}

void test_commit_failures_and_race_authority() {
    for (const auto status : {
             SupportBundleIntakeStateCommitStatus::invalid_input,
             SupportBundleIntakeStateCommitStatus::unsafe_root,
             SupportBundleIntakeStateCommitStatus::unsafe_existing_state,
             SupportBundleIntakeStateCommitStatus::rollback,
             SupportBundleIntakeStateCommitStatus::same_generation_mutated,
             SupportBundleIntakeStateCommitStatus::temporary_collision,
             SupportBundleIntakeStateCommitStatus::write_failed,
             SupportBundleIntakeStateCommitStatus::publish_failed}) {
        std::vector<std::string> calls;
        auto dependencies = successful_dependencies(calls);
        dependencies.commit_state = [&](const fs::path &, const SupportBundleIntakeState &) {
            calls.push_back("commit");
            return SupportBundleIntakeStateCommitResult{status};
        };
        const auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
        assert(result.failure == SupportBundleIntakeControllerFailure::state_commit_failed);
        assert(result.state_commit_status == status);
        assert(calls == std::vector<std::string>({"load", "retrieve", "validate", "commit"}));
        assert_no_manifest(result);
        assert(!result.upgrade);
    }
}

void test_missing_dependency_fails_closed() {
    SupportBundleIntakeControllerDependencies dependencies;
    const auto result = resolve_support_bundle_intake_for_test(basic_request(), dependencies);
    assert(result.failure == SupportBundleIntakeControllerFailure::state_load_failed);
    assert_no_manifest(result);
    assert(!result.upgrade);
}

void test_real_validation_state_uncertain_retry() {
    auto signing = make_signing_fixture();
    const auto document = [&](std::uint64_t generation,
                              const std::string &minimum,
                              std::uint64_t protocol = 1,
                              bool active = true) {
        auto value = json({
            {"schema_version", 1}, {"project_id", "wsprrypi"},
            {"generation", generation}, {"published_at", "2026-08-17T00:00:00Z"},
            {"expires_at", "2026-08-18T00:00:00Z"},
            {"status", active ? "active" : "disabled"},
            {"minimum_client_protocol", protocol}, {"minimum_upload_version", minimum},
            {"release_url", "https://github.com/WsprryPi/WsprryPi/releases/latest"},
            {"user_message", nullptr},
            {"bundle_encryption_key_id", "wsprrypi-bundle-2099-99"},
        });
        if (active)
            value["request_url"] = "https://www.dropbox.com/request/TestOpaque_123";
        const auto manifest = value.dump();
        return std::pair{manifest, signed_envelope(signing.key.get(), manifest)};
    };
    auto [fetched_manifest, fetched_envelope] = document(8, "9.0.0");
    SupportBundleIntakeControllerRequest request;
    request.signing_keys = {signing.pinned};
    request.recognized_bundle_key_ids = {"wsprrypi-bundle-2099-99"};
    request.installed_upload_version = "1.3.0";
    request.now_utc_seconds = 1786986000;

    auto retrieval = [&](const SupportBundleIntakeRetrievalRequest &) {
        return SupportBundleIntakeRetrievalResult{
            SupportBundleIntakeRetrievalFailure::none, fetched_manifest, fetched_envelope};
    };

    TemporaryRoot disabled_root;
    request.state_root = disabled_root.path;
    std::tie(fetched_manifest, fetched_envelope) = document(8, "9.0.0", 1, false);
    SupportBundleIntakeControllerDependencies concrete = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        commit_support_bundle_intake_state,
    };
    auto result = resolve_support_bundle_intake_for_test(request, concrete);
    assert(result.ready() && result.manifest.status == "disabled");
    assert(!result.manifest.request_url && !result.upgrade);
    auto loaded = load_support_bundle_intake_state(disabled_root.path);
    assert(loaded.loaded() && loaded.state.generation == 8);

    std::tie(fetched_manifest, fetched_envelope) = document(8, "9.0.0");

    TemporaryRoot upgrade_root;
    request.state_root = upgrade_root.path;
    request.installed_upload_version = "1.3.0";
    SupportBundleIntakeControllerDependencies upgrade = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        commit_support_bundle_intake_state,
    };
    result = resolve_support_bundle_intake_for_test(request, upgrade);
    assert(result.failure == SupportBundleIntakeControllerFailure::validation_failed);
    assert(result.validation_failure == SupportBundleIntakeFailure::upgrade_required);
    assert_no_manifest(result);
    assert(result.upgrade && result.upgrade->minimum_upload_version == "9.0.0");
    loaded = load_support_bundle_intake_state(upgrade_root.path);
    assert(loaded.loaded() && loaded.state.generation == 8);

    std::tie(fetched_manifest, fetched_envelope) = document(7, "1.0.0");
    result = resolve_support_bundle_intake_for_test(request, upgrade);
    assert(result.failure == SupportBundleIntakeControllerFailure::validation_failed);
    assert(result.validation_failure == SupportBundleIntakeFailure::rollback);
    assert_no_manifest(result);
    assert(!result.upgrade);
    loaded = load_support_bundle_intake_state(upgrade_root.path);
    assert(loaded.loaded() && loaded.state.generation == 8);

    TemporaryRoot protocol_root;
    request.state_root = protocol_root.path;
    std::tie(fetched_manifest, fetched_envelope) = document(8, "9.0.0", 2);
    result = resolve_support_bundle_intake_for_test(request, upgrade);
    assert(result.failure == SupportBundleIntakeControllerFailure::validation_failed);
    assert(result.validation_failure == SupportBundleIntakeFailure::incompatible_client);
    assert_no_manifest(result);
    assert(!result.upgrade);
    assert(load_support_bundle_intake_state(protocol_root.path).status ==
           SupportBundleIntakeStateLoadStatus::absent);

    TemporaryRoot upgrade_competing_root;
    request.state_root = upgrade_competing_root.path;
    std::tie(fetched_manifest, fetched_envelope) = document(8, "9.0.0");
    SupportBundleIntakeControllerDependencies upgrade_competing = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        [&](const fs::path &path, const SupportBundleIntakeState &state) {
            assert(commit_support_bundle_intake_state(path, {9, kDigestB}).published());
            return commit_support_bundle_intake_state(path, state);
        },
    };
    result = resolve_support_bundle_intake_for_test(request, upgrade_competing);
    assert(result.failure == SupportBundleIntakeControllerFailure::state_commit_failed);
    assert(result.state_commit_status == SupportBundleIntakeStateCommitStatus::rollback);
    assert_no_manifest(result);
    assert(!result.upgrade);

    TemporaryRoot upgrade_uncertain_root;
    request.state_root = upgrade_uncertain_root.path;
    SupportBundleIntakeControllerDependencies upgrade_uncertain = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        [&](const fs::path &path, const SupportBundleIntakeState &state) {
            return commit_support_bundle_intake_state_for_test(
                path, state, {SupportBundleIntakeStateTestFault::directory_sync, {}});
        },
    };
    result = resolve_support_bundle_intake_for_test(request, upgrade_uncertain);
    assert(result.failure == SupportBundleIntakeControllerFailure::state_durability_uncertain);
    assert_no_manifest(result);
    assert(!result.upgrade);
    result = resolve_support_bundle_intake_for_test(request, upgrade);
    assert(result.failure == SupportBundleIntakeControllerFailure::validation_failed);
    assert(result.validation_failure == SupportBundleIntakeFailure::upgrade_required);
    assert(result.state_commit_status == SupportBundleIntakeStateCommitStatus::unchanged);
    assert_no_manifest(result);
    assert(result.upgrade && result.upgrade->minimum_upload_version == "9.0.0");

    std::tie(fetched_manifest, fetched_envelope) = document(7, "1.3.0");

    TemporaryRoot advanced_root;
    request.state_root = advanced_root.path;
    SupportBundleIntakeControllerDependencies advanced = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        [&](const fs::path &path, const SupportBundleIntakeState &state) {
            assert(commit_support_bundle_intake_state(path, {8, kDigestB}).published());
            return commit_support_bundle_intake_state(path, state);
        },
    };
    result = resolve_support_bundle_intake_for_test(request, advanced);
    assert(result.failure == SupportBundleIntakeControllerFailure::state_commit_failed);
    assert(result.state_commit_status == SupportBundleIntakeStateCommitStatus::rollback);
    assert_no_manifest(result);
    loaded = load_support_bundle_intake_state(advanced_root.path);
    assert(loaded.loaded() && loaded.state.generation == 8 &&
           loaded.state.manifest_sha256 == kDigestB);

    TemporaryRoot root;
    request.state_root = root.path;
    SupportBundleIntakeControllerDependencies uncertain = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        [&](const fs::path &path, const SupportBundleIntakeState &state) {
            return commit_support_bundle_intake_state_for_test(
                path, state, {SupportBundleIntakeStateTestFault::directory_sync, {}});
        },
    };
    result = resolve_support_bundle_intake_for_test(request, uncertain);
    assert(result.failure == SupportBundleIntakeControllerFailure::state_durability_uncertain);
    assert(result.state_commit_status ==
           SupportBundleIntakeStateCommitStatus::committed_sync_uncertain);
    assert_no_manifest(result);
    loaded = load_support_bundle_intake_state(root.path);
    assert(loaded.loaded() && loaded.state.generation == 7);

    SupportBundleIntakeControllerDependencies retry = {
        load_support_bundle_intake_state,
        retrieval,
        validate_support_bundle_intake,
        commit_support_bundle_intake_state,
    };
    result = resolve_support_bundle_intake_for_test(request, retry);
    assert(result.ready() && result.manifest.generation == 7);
    assert(result.state_commit_status == SupportBundleIntakeStateCommitStatus::unchanged);
    assert(result.manifest.request_url ==
           "https://www.dropbox.com/request/TestOpaque_123");
}

} // namespace

int main() {
    test_order_absent_loaded_and_success();
    test_load_failures_stop_before_retrieval();
    test_retrieval_and_validation_failures_stop_later_stages();
    test_upgrade_requirement_is_limited_and_committed_before_disclosure();
    test_commit_failures_and_race_authority();
    test_missing_dependency_fails_closed();
    test_real_validation_state_uncertain_retry();
    std::cout << "support_bundle_intake_controller_test: PASS\n";
}
