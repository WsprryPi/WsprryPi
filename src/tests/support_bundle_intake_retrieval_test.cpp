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
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <cerrno>
#include <sys/wait.h>

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

SupportBundleIntakeRetrievalResult scenario_result(Fixture& fixture, const std::string& name, bool pending_reap = false) {
    const auto executable = fixture.scenario(name);
    const bool held = name=="manifest-timeout" || name=="signature-timeout" || name=="signature-descendant";
    if (!held) return retrieve_support_bundle_intake_for_test(request_for(executable));
    const auto ready = executable.string()+".ready";
    const auto release = executable.string()+".release";
    assert(mkfifo(ready.c_str(),0600)==0 && mkfifo(release.c_str(),0600)==0);
    const int lifetime = open(ready.c_str(),O_RDONLY|O_NONBLOCK);
    const int release_owner = open(release.c_str(),O_RDWR|O_NONBLOCK);
    assert(lifetime>=0 && release_owner>=0);
    int descendant = -1, group = -1;
    const auto fail = [](const char* stage) { throw std::runtime_error(std::string("stage=")+stage+
        "; expected=owned fixture transition; last_state=held; hang_guard only, not product latency"); };
    try {
        SupportBundleIntakeRetrievalTestHooks hooks;
        hooks.force_reap_pending = pending_reap;
        hooks.after_spawn = [&](int owner, const std::string& url) {
            if ((name=="manifest-timeout") != (url==kWsprryPiIntakeManifestUrl)) return;
            const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
            std::string acknowledgment;
            while (acknowledgment.find('\n')==std::string::npos) {
                if (std::chrono::steady_clock::now()>=deadline) fail("retrieval-child-entry");
                pollfd descriptor{lifetime,POLLIN,0};
                if (poll(&descriptor,1,50)<0 && errno!=EINTR) fail("retrieval-entry-channel");
                char bytes[128]; const auto count = read(lifetime,bytes,sizeof(bytes));
                if (count>0) acknowledgment.append(bytes,static_cast<std::size_t>(count));
                if (acknowledgment.size()>128) fail("retrieval-entry-shape");
            }
            std::istringstream identity(acknowledgment);
            if (!(identity>>descendant>>group) || descendant<=0 || group!=owner || getpgid(descendant)!=owner)
                fail("retrieval-entry-identity");
        };
        const auto result = retrieve_support_bundle_intake_for_test(request_for(executable),hooks);
        if (descendant<=0) fail("retrieval-missing-ack");
        // EOF from a channel held by the acknowledged branch proves its exit.
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while (true) {
            char byte; const auto count = read(lifetime,&byte,1);
            if (count==0) break;
            if (count>0 || (errno!=EAGAIN && errno!=EINTR)) fail("retrieval-cleanup-channel");
            if (std::chrono::steady_clock::now()>=deadline) fail("retrieval-descendant-cleanup");
            pollfd descriptor{lifetime,POLLIN|POLLHUP,0}; (void)poll(&descriptor,1,50);
        }
        if (pending_reap) {
            assert(result.cleanup_diagnostic.find("stage=intake-child-cleanup")!=std::string::npos);
            int launches = 0;
            SupportBundleIntakeRetrievalTestHooks blocked;
            blocked.force_reap_pending = true;
            blocked.after_spawn = [&](int,const std::string&) { ++launches; };
            const auto refusal = retrieve_support_bundle_intake_for_test(request_for(executable),blocked);
            assert(launches==0 && refusal.failure==SupportBundleIntakeRetrievalFailure::launch_failed &&
                   refusal.cleanup_diagnostic.find("before another launch")!=std::string::npos);
            const auto reap_deadline = std::chrono::steady_clock::now()+std::chrono::seconds(3);
            int waited = 0;
            do {
                waited = waitpid(group,nullptr,WNOHANG);
                if (waited==0) (void)poll(nullptr,0,1);
            } while (waited==0 && std::chrono::steady_clock::now()<reap_deadline);
            if (waited!=group) fail("retrieval-fixture-reap");
            assert(retrieve_support_bundle_intake_for_test(request_for(fixture.helper)).retrieved());
        } else if (!result.cleanup_diagnostic.empty()) fail("retrieval-unexpected-cleanup-failure");
        close(lifetime); close(release_owner);
        return result;
    } catch (...) {
        if (group>0) kill(-group,SIGKILL);
        close(lifetime); close(release_owner);
        throw;
    }
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
        assert_empty(scenario_result(fixture,scenario), failure);
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
        assert_empty(scenario_result(fixture,scenario), failure);
    }
    assert_empty(scenario_result(fixture,"signature-descendant"),
                 SupportBundleIntakeRetrievalFailure::signature_timeout);
    Fixture pending(fixture.helper);
    assert_empty(scenario_result(pending,"signature-timeout",true),
                 SupportBundleIntakeRetrievalFailure::signature_timeout);
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
