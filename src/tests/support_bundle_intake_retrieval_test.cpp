#include "support_bundle_intake_retrieval.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Fixture {
    fs::path root;
    fs::path helper;
    Fixture(const fs::path &source) {
        std::string pattern = (fs::temp_directory_path() / "wsprrypi-intake-fetch-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        const auto created = mkdtemp(buffer.data());
        assert(created);
        root = created;
        helper = root / "success";
        fs::copy_file(source, helper);
        assert(chmod(helper.c_str(), 0755) == 0);
    }
    ~Fixture() { fs::remove_all(root); }
    fs::path scenario(const std::string &name) {
        const auto path = root / name;
        fs::copy_file(helper, path);
        assert(chmod(path.c_str(), 0755) == 0);
        return path;
    }
};

SupportBundleIntakeRetrievalRequest request_for(const fs::path &executable) {
    SupportBundleIntakeRetrievalRequest request;
    request.curl_executable = executable;
    request.connect_timeout = std::chrono::milliseconds(100);
    request.operation_timeout = std::chrono::milliseconds(800);
    return request;
}

void assert_empty(const SupportBundleIntakeRetrievalResult &result,
                  SupportBundleIntakeRetrievalFailure expected) {
    if (result.failure != expected) {
        std::cerr << "unexpected retrieval failure: got "
                  << static_cast<int>(result.failure) << ", expected "
                  << static_cast<int>(expected) << '\n';
    }
    assert(result.failure == expected);
    assert(result.manifest_bytes.empty() && result.signature_envelope_bytes.empty());
}

void test_success_and_binary_exactness(Fixture &fixture) {
    const auto result = retrieve_support_bundle_intake_for_test(request_for(fixture.helper));
    assert(result.retrieved());
    assert(result.manifest_bytes == std::string("manifest\0bytes\n", 15));
    assert(result.signature_envelope_bytes == std::string("signature\0bytes\n", 16));
}

void test_request_and_executable_policy(Fixture &fixture) {
    auto request = request_for(fixture.helper);
    request.manifest_url = "http://raw.githubusercontent.com/WsprryPi/support-intake/main/wsprrypi/intake.json";
    assert_empty(retrieve_support_bundle_intake_for_test(request),
                 SupportBundleIntakeRetrievalFailure::invalid_request);
    request = request_for(fixture.helper);
    request.signature_url += "?unexpected=1";
    assert_empty(retrieve_support_bundle_intake_for_test(request),
                 SupportBundleIntakeRetrievalFailure::invalid_request);
    request = request_for(fixture.helper);
    request.maximum_manifest_bytes = 1;
    assert_empty(retrieve_support_bundle_intake_for_test(request),
                 SupportBundleIntakeRetrievalFailure::invalid_request);
    request = request_for(fixture.helper);
    request.operation_timeout = std::chrono::seconds(61);
    assert_empty(retrieve_support_bundle_intake_for_test(request),
                 SupportBundleIntakeRetrievalFailure::invalid_request);
    request = request_for("relative-curl");
    assert_empty(retrieve_support_bundle_intake_for_test(request),
                 SupportBundleIntakeRetrievalFailure::executable_unavailable);
    const auto symlink = fixture.root / "curl-link";
    fs::create_symlink(fixture.helper, symlink);
    assert_empty(retrieve_support_bundle_intake_for_test(request_for(symlink)),
                 SupportBundleIntakeRetrievalFailure::executable_unavailable);
    const auto writable = fixture.scenario("writable");
    assert(chmod(writable.c_str(), 0775) == 0);
    assert_empty(retrieve_support_bundle_intake_for_test(request_for(writable)),
                 SupportBundleIntakeRetrievalFailure::executable_unavailable);
    const auto invalid = fixture.root / "invalid-executable";
    std::ofstream(invalid) << "not an executable format";
    assert(chmod(invalid.c_str(), 0755) == 0);
    assert_empty(retrieve_support_bundle_intake_for_test(request_for(invalid)),
                 SupportBundleIntakeRetrievalFailure::launch_failed);
    assert_empty(retrieve_support_bundle_intake(request_for(fixture.helper)),
                 SupportBundleIntakeRetrievalFailure::executable_unavailable);
    assert_empty(retrieve_support_bundle_intake_for_test(
                     request_for(fixture.helper), {true}),
                 SupportBundleIntakeRetrievalFailure::launch_failed);
}

void test_manifest_failures(Fixture &fixture) {
    for (const auto &[scenario, failure] : {
             std::pair{"manifest-empty", SupportBundleIntakeRetrievalFailure::manifest_empty},
             std::pair{"manifest-fail", SupportBundleIntakeRetrievalFailure::manifest_failed},
             std::pair{"manifest-oversized", SupportBundleIntakeRetrievalFailure::manifest_oversized},
             std::pair{"manifest-timeout", SupportBundleIntakeRetrievalFailure::manifest_timeout}}) {
        assert_empty(retrieve_support_bundle_intake_for_test(request_for(fixture.scenario(scenario))), failure);
    }
}

void test_signature_failures_discard_manifest(Fixture &fixture) {
    for (const auto &[scenario, failure] : {
             std::pair{"signature-empty", SupportBundleIntakeRetrievalFailure::signature_empty},
             std::pair{"signature-fail", SupportBundleIntakeRetrievalFailure::signature_failed},
             std::pair{"signature-redirect", SupportBundleIntakeRetrievalFailure::signature_failed},
             std::pair{"signature-signal", SupportBundleIntakeRetrievalFailure::signature_failed},
             std::pair{"signature-oversized", SupportBundleIntakeRetrievalFailure::signature_oversized},
             std::pair{"signature-timeout", SupportBundleIntakeRetrievalFailure::signature_timeout}}) {
        assert_empty(retrieve_support_bundle_intake_for_test(request_for(fixture.scenario(scenario))), failure);
    }
    const auto descendant = fixture.scenario("signature-descendant");
    assert_empty(retrieve_support_bundle_intake_for_test(request_for(descendant)),
                 SupportBundleIntakeRetrievalFailure::signature_timeout);
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    assert(!fs::exists(descendant.string() + ".survived"));
}

} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);
    Fixture fixture(fs::absolute(argv[1]));
    test_success_and_binary_exactness(fixture);
    test_request_and_executable_policy(fixture);
    test_manifest_failures(fixture);
    test_signature_failures_discard_manifest(fixture);
    std::cout << "support_bundle_intake_retrieval_test: PASS\n";
}
