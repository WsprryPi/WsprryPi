#include "support_bundle_http.hpp"

#include "json.hpp"
#include "support_bundle_encryption_production.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
std::string job_id(char value) {
    return std::string(32, value);
}

class FakeExecutor final : public SupportBundleJobExecutor {
public:
    enum class Mode {
        success,
        failure,
        corrupt_archive,
        corrupt_sidecar,
        unsafe_sidecar,
        missing_archive,
    };

    SupportBundleExecutionResult run(const SupportBundleExecutionContext &context) override {
        std::unique_lock lock(mutex_);
        context_ = context;
        entered_ = true;
        entered_condition_.notify_all();
        release_condition_.wait(lock, [this] { return released_ || stopped_; });
        if (stopped_) {
            return {false, "stopped", "raw executor output /secret"};
        }
        if (mode_ == Mode::failure) {
            return {false, "raw:/secret", "raw executor output /secret"};
        }

        const bool probe = context.probe_i2c;
        constexpr const char *digest =
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        const fs::path archive = context.job_directory / "WsprryPi-support-test.tar.gz";
        const fs::path checksum = archive.string() + ".sha256";
        nlohmann::json result = {
            {"schema_version", 1},
            {"status", "success"},
            {"archive_filename", "WsprryPi-support-test.tar.gz"},
            {"sha256_filename", "WsprryPi-support-test.tar.gz.sha256"},
            {"sha256", digest},
            {"generated_at_utc", "20260101T000000Z"},
            {"configuration_files_included", false},
            {"full_logs_included", false},
            {"i2c_probe_requested", probe},
            {"i2c_probe_status", probe ? "succeeded" : "skipped_by_user"},
            {"privileged_diagnostics_may_be_incomplete", false},
        };
        if (!context.case_id.empty()) {
            result["case_id"] = context.case_id;
            result["manifest_included"] = true;
        }
        if (mode_ != Mode::missing_archive) {
            std::ofstream(archive) << (mode_ == Mode::corrupt_archive ? "def" : "abc");
            assert(chmod(archive.c_str(), 0600) == 0);
        }
        if (mode_ == Mode::unsafe_sidecar) {
            assert(symlink("/tmp", checksum.c_str()) == 0);
        } else {
            std::ofstream(checksum) <<
                (mode_ == Mode::corrupt_sidecar
                     ? std::string(64, 'b') + "  WsprryPi-support-test.tar.gz\n"
                     : std::string(digest) + "  WsprryPi-support-test.tar.gz\n");
            assert(chmod(checksum.c_str(), 0600) == 0);
        }
        std::ofstream(context.job_directory / "WsprryPi-support-test.tar.gz.result.json")
            << result.dump();
        return {true, {}, {}};
    }

    void request_stop() noexcept override {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        release_condition_.notify_all();
    }

    void wait_entered() {
        std::unique_lock lock(mutex_);
        assert(entered_condition_.wait_for(lock, 2s, [this] { return entered_; }));
    }

    void release(Mode mode) {
        std::lock_guard lock(mutex_);
        mode_ = mode;
        entered_ = false;
        released_ = true;
        release_condition_.notify_all();
    }

    void reset() {
        std::lock_guard lock(mutex_);
        released_ = false;
        stopped_ = false;
    }

private:
    std::mutex mutex_;
    std::condition_variable entered_condition_;
    std::condition_variable release_condition_;
    SupportBundleExecutionContext context_;
    bool entered_ = false;
    bool released_ = false;
    bool stopped_ = false;
    Mode mode_ = Mode::success;
};

const httplib::Response &require_response(const httplib::Result &result) {
    assert(result);
    return result.value();
}

nlohmann::json response_json(const httplib::Response &response) {
    return nlohmann::json::parse(response.body);
}

void assert_restrictive(const httplib::Response &response) {
    assert(!response.has_header("Access-Control-Allow-Origin"));
    assert(!response.has_header("Access-Control-Allow-Methods"));
    assert(!response.has_header("Access-Control-Allow-Headers"));
}

void assert_no_private_download_metadata(const nlohmann::json &body) {
    assert(!body.contains("archive_path"));
    assert(!body.contains("archive_basename"));
    assert(!body.contains("archive_filename"));
    assert(!body.contains("checksum_path"));
    assert(!body.contains("checksum_basename"));
    assert(!body.contains("checksum_filename"));
    assert(!body.contains("expected_sha256"));
    assert(!body.contains("sha256"));
}

httplib::Headers local_headers(int port) {
    return {{"Host", "127.0.0.1:" + std::to_string(port)},
            {"Origin", "http://127.0.0.1:" + std::to_string(port)}};
}

httplib::Response get_status(httplib::Client &client,
                             const httplib::Headers &headers,
                             const std::string &id) {
    return require_response(client.Get("/api/support-bundles/" + id, headers));
}

httplib::Response get_download(httplib::Client &client,
                               const httplib::Headers &headers,
                               const std::string &id) {
    return require_response(
        client.Get("/api/support-bundles/" + id + "/download", headers));
}

httplib::Response delete_bundle(httplib::Client &client,
                                const httplib::Headers &headers,
                                const std::string &id) {
    return require_response(client.Delete("/api/support-bundles/" + id, headers));
}

httplib::Response finalize_bundle(httplib::Client &client,
                                  const httplib::Headers &headers,
                                  const std::string &id) {
    return require_response(client.Post(
        "/api/support-bundles/" + id + "/finalize", headers, "", "text/plain"));
}

void assert_no_private_download_leak(const httplib::Response &response,
                                     const fs::path &storage_root) {
    assert(response.body.find(storage_root.string()) == std::string::npos);
    assert(response.body.find("sha256") == std::string::npos);
    assert(response.body.find("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") ==
           std::string::npos);
    assert(response.body.find("WsprryPi-support-test.tar.gz.sha256") == std::string::npos);
    assert(response.body.find("raw executor output") == std::string::npos);
}

void wait_for_state(httplib::Client &client,
                    const httplib::Headers &headers,
                    const std::string &id,
                    const char *expected) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto response = get_status(client, headers, id);
        if (response.status == 200 && response_json(response)["state"] == expected) {
            return;
        }
        std::this_thread::sleep_for(5ms);
    }
    assert(false);
}

void wait_for_workflow_state(httplib::Client &client,
                             const httplib::Headers &headers,
                             const std::string &id,
                             const char *expected) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto response = get_status(client, headers, id);
        if (response.status == 200 &&
            response_json(response)["workflow_state"] == expected) {
            return;
        }
        std::this_thread::sleep_for(5ms);
    }
    assert(false);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--encrypt") {
        std::ifstream input(argv[6], std::ios::binary);
        std::ofstream output(argv[5], std::ios::binary);
        output << "encrypted:" << input.rdbuf();
        output.close();
        return chmod(argv[5], 0600) == 0 ? 0 : 1;
    }
    char template_path[] = "/tmp/wsprrypi-support-bundle-http-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const fs::path storage_root(template_path);
    assert(chmod(storage_root.c_str(), 0700) == 0);
    const auto fake_age = storage_root / "fake-age";
    assert(fs::copy_file(fs::canonical(argv[0]), fake_age));
    assert(chmod(fake_age.c_str(), 0500) == 0);

    const auto executor = std::make_shared<FakeExecutor>();
    const auto provider_calls = std::make_shared<int>(0);
    const auto fail_cleanup = std::make_shared<std::atomic<bool>>(false);
    int intake_calls = 0;
    bool intake_throw = false;
    SupportBundleIntakeProductionResult intake_result;
    int id_index = 0;
    SupportBundleJobManager manager(
        executor, [&] { return job_id(std::string_view("abcdef0123456789")[id_index++]); }, storage_root,
        [fail_cleanup](const fs::path &root, const std::string &id) {
            if (fail_cleanup->load()) {
                return SupportBundleJobDirectoryRemovalResult{
                    SupportBundleJobDirectoryRemovalFailure::removal_failed};
            }
            return remove_support_bundle_job_directory(root, id);
        }, SupportBundleJobManager::kProductionRetention,
        SupportBundleJobManager::kProductionRetryDelay,
        [] { return std::optional<std::string>("7K3M-9QFX-2DPA"); });
    httplib::Server server;
    server.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                                {"Access-Control-Allow-Methods", "GET, POST"},
                                {"Access-Control-Allow-Headers", "Content-Type"}});
    register_support_bundle_http_routes(server, manager, [provider_calls] {
        ++*provider_calls;
        return SupportRequestGuardSnapshot{true, "test-pi", {}, {}};
    }, [&] {
        ++intake_calls;
        if (intake_throw) throw std::runtime_error("private provider failure");
        return intake_result;
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    assert(port > 0);
    std::thread server_thread([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    httplib::Client client("127.0.0.1", port);
    const auto headers = local_headers(port);

    const auto unavailable_intake =
        require_response(client.Get("/api/support-intake", headers));
    assert(unavailable_intake.status == 503);
    assert(response_json(unavailable_intake) ==
           nlohmann::json({{"status", "unavailable"}}));
    assert(unavailable_intake.get_header_value("Cache-Control") == "no-store");
    assert(unavailable_intake.get_header_value("X-Content-Type-Options") == "nosniff");
    assert_restrictive(unavailable_intake);
    assert(intake_calls == 1);

    intake_result.status = SupportBundleIntakeProductionStatus::active;
    intake_result.generation = 7;
    intake_result.expires_at = "2026-09-01T00:00:00Z";
    intake_result.minimum_upload_version = "1.2.3";
    intake_result.signing_key_id = "wsprrypi-intake-2026-01";
    intake_result.bundle_key_id = "wsprrypi-bundle-2026-01";
    intake_result.request_url = "https://www.dropbox.com/request/test-capability";
    intake_result.user_message = "Support intake is available.";
    const auto active_intake = require_response(client.Get("/api/support-intake", headers));
    const auto active_body = response_json(active_intake);
    assert(active_intake.status == 200 && active_body.size() == 8);
    assert(active_body["status"] == "active" && active_body["generation"] == 7);
    assert(active_body["request_url"] == *intake_result.request_url);
    assert(active_body["user_message"] == *intake_result.user_message);
    assert(!active_body.contains("release_url") && !active_body.contains("diagnostic"));
    assert(intake_calls == 2);
    const std::string unknown_private_id(32, 'Z');
    const auto encrypt_body_rejected = require_response(client.Post(
        "/api/support-bundles/" + unknown_private_id + "/encrypt", headers,
        "{}", "application/json"));
    assert(encrypt_body_rejected.status == 400 && intake_calls == 2);
    const httplib::Headers foreign_encrypt{
        {"Host", "127.0.0.1:" + std::to_string(port)},
        {"Origin", "https://example.invalid"}};
    const auto encrypt_guarded = require_response(client.Post(
        "/api/support-bundles/" + unknown_private_id + "/encrypt", foreign_encrypt));
    assert(encrypt_guarded.status == 403 && intake_calls == 2);
    const auto encrypt_unknown = require_response(client.Post(
        "/api/support-bundles/" + unknown_private_id + "/encrypt", headers));
    assert(encrypt_unknown.status == 404 && intake_calls == 3);
    assert(encrypt_unknown.get_header_value("Cache-Control") == "no-store");
    assert_restrictive(encrypt_unknown);

    intake_result = {};
    intake_result.status = SupportBundleIntakeProductionStatus::disabled;
    intake_result.generation = 8;
    intake_result.expires_at = "2026-09-02T00:00:00Z";
    intake_result.signing_key_id = "wsprrypi-intake-2026-01";
    intake_result.bundle_key_id = "wsprrypi-bundle-2026-01";
    const auto disabled_intake = require_response(client.Get("/api/support-intake", headers));
    const auto disabled_body = response_json(disabled_intake);
    assert(disabled_intake.status == 200 && disabled_body.size() == 5);
    assert(disabled_body["status"] == "disabled");
    assert(!disabled_body.contains("request_url") &&
           !disabled_body.contains("minimum_upload_version"));

    intake_result = {};
    intake_result.status = SupportBundleIntakeProductionStatus::upgrade_required;
    intake_result.minimum_upload_version = "2.0.0";
    intake_result.release_url = "https://github.com/WsprryPi/WsprryPi/releases";
    intake_result.user_message = "Upgrade before uploading.";
    const auto upgrade_intake = require_response(client.Get("/api/support-intake", headers));
    const auto upgrade_body = response_json(upgrade_intake);
    assert(upgrade_intake.status == 200 && upgrade_body.size() == 4);
    assert(upgrade_body["status"] == "upgrade_required");
    assert(!upgrade_body.contains("request_url") && !upgrade_body.contains("generation"));

    intake_result.status = SupportBundleIntakeProductionStatus::disabled;
    intake_result.request_url = "https://www.dropbox.com/request/must-not-leak";
    const auto malformed_intake = require_response(client.Get("/api/support-intake", headers));
    assert(malformed_intake.status == 503);
    assert(malformed_intake.body.find("must-not-leak") == std::string::npos);
    assert(response_json(malformed_intake) ==
           nlohmann::json({{"status", "unavailable"}}));

    intake_throw = true;
    const auto throwing_intake = require_response(client.Get("/api/support-intake", headers));
    assert(throwing_intake.status == 503);
    assert(response_json(throwing_intake) ==
           nlohmann::json({{"status", "unavailable"}}));
    intake_throw = false;

    const int calls_before_rejection = intake_calls;
    assert(calls_before_rejection == 7);
    const httplib::Headers foreign_intake{
        {"Host", "127.0.0.1:" + std::to_string(port)},
        {"Origin", "https://example.invalid"}};
    const auto rejected_intake =
        require_response(client.Get("/api/support-intake", foreign_intake));
    assert(rejected_intake.status == 403 && intake_calls == calls_before_rejection);
    assert(rejected_intake.body.find("dropbox") == std::string::npos);
    assert(rejected_intake.get_header_value("Cache-Control") == "no-store");
    assert(rejected_intake.get_header_value("X-Content-Type-Options") == "nosniff");
    const auto intake_options =
        require_response(client.Options("/api/support-intake", headers));
    assert(intake_options.status == 204 && intake_calls == calls_before_rejection);
    assert_restrictive(intake_options);
    assert(intake_options.get_header_value("Cache-Control") == "no-store");

    const int handoff_start = intake_calls;
    const std::string unknown_handoff_path =
        "/api/support-bundles/" + job_id('z') + "/handoff";
    const auto unavailable_handoff =
        require_response(client.Get(unknown_handoff_path, headers));
    assert(unavailable_handoff.status == 409 && intake_calls == handoff_start);
    assert(!unavailable_handoff.has_header("Location"));
    assert(unavailable_handoff.get_header_value("Referrer-Policy") == "no-referrer");

    httplib::Request handoff_with_body;
    handoff_with_body.method = "GET";
    handoff_with_body.path = unknown_handoff_path;
    handoff_with_body.headers = headers;
    handoff_with_body.body = "unexpected";
    const auto body_rejected = require_response(client.send(handoff_with_body));
    assert(body_rejected.status == 400 && intake_calls == handoff_start);
    assert(body_rejected.get_header_value("Referrer-Policy") == "no-referrer");

    const auto guarded_handoff =
        require_response(client.Get(unknown_handoff_path, foreign_intake));
    assert(guarded_handoff.status == 403 && intake_calls == handoff_start);
    assert(guarded_handoff.get_header_value("Referrer-Policy") == "no-referrer");
    *provider_calls = 0;

    const auto created = require_response(client.Post(
        "/api/support-bundles", headers, "{}", "ApPlIcAtIoN/JsOn ; ChArSeT = UtF-8"));
    assert(created.status == 202);
    assert(*provider_calls == 1);
    assert_restrictive(created);
    const auto created_body = response_json(created);
    const std::string first_id = created_body["id"];
    assert(first_id == job_id('a'));
    assert(created_body["state"] == "queued");
    assert(created_body["probe_i2c_requested"] == false);
    assert(created_body["i2c_probe_status"] == "");
    assert(!created_body["download_available"]);
    assert_no_private_download_metadata(created_body);

    const auto queued_delete = delete_bundle(client, headers, first_id);
    assert(queued_delete.status == 409 && response_json(queued_delete)["error"] == "not_terminal");
    assert_restrictive(queued_delete);
    assert_no_private_download_leak(queued_delete, storage_root);

    executor->wait_entered();
    wait_for_state(client, headers, first_id, "running");
    const auto not_ready_download = get_download(client, headers, first_id);
    assert(not_ready_download.status == 409);
    assert(response_json(not_ready_download)["error"] == "not_ready");
    assert_restrictive(not_ready_download);
    assert_no_private_download_leak(not_ready_download, storage_root);
    const int calls_before_later_request = *provider_calls;
    const auto retained_provider = get_status(client, headers, first_id);
    assert(retained_provider.status == 200);
    assert(*provider_calls == calls_before_later_request + 1);
    const auto conflict = require_response(client.Post("/api/support-bundles", headers, "{\"probe_i2c\":true}", "application/json"));
    assert(conflict.status == 409);
    assert_restrictive(conflict);

    const std::vector<std::tuple<std::string, std::string, int>> invalid_requests = {
        {"{", "application/json", 400},
        {"{}", "text/plain", 415},
        {"{\"unknown\":true}", "application/json", 400},
        {"{\"probe_i2c\":\"true\"}", "application/json", 400},
        {std::string(8193, 'x'), "application/json", 413},
        {"{}", "application/json; charset=iso-8859-1", 415},
        {"{}", "application/json; boundary=test", 415},
        {"{}", "application/json; charset", 415},
    };
    for (const auto &[body, content_type, expected_status] : invalid_requests) {
        const auto response = require_response(client.Post("/api/support-bundles", headers, body, content_type));
        assert(response.status == expected_status);
        assert_restrictive(response);
    }

    executor->release(FakeExecutor::Mode::success);
    wait_for_state(client, headers, first_id, "succeeded");
    const auto succeeded = get_status(client, headers, first_id);
    assert(response_json(succeeded)["i2c_probe_status"] == "skipped_by_user");
    assert(response_json(succeeded)["download_available"] == true);
    assert_no_private_download_metadata(response_json(succeeded));
    const auto downloaded = get_download(client, headers, first_id);
    assert(downloaded.status == 200);
    assert(downloaded.body == "abc");
    assert(downloaded.get_header_value("Content-Type") == "application/gzip");
    assert(downloaded.get_header_value("Content-Length") == "3");
    assert(downloaded.get_header_value("Content-Disposition") ==
           "attachment; filename=\"WsprryPi-support-test.tar.gz\"");
    assert(downloaded.get_header_value("Cache-Control") == "no-store");
    assert(downloaded.get_header_value("X-Content-Type-Options") == "nosniff");
    assert_restrictive(downloaded);
    assert_no_private_download_leak(downloaded, storage_root);

    const httplib::Headers foreign_origin{{"Host", "127.0.0.1:" + std::to_string(port)},
                                          {"Origin", "http://example.invalid"}};
    const auto guarded_delete = delete_bundle(client, foreign_origin, first_id);
    assert(guarded_delete.status == 403);
    assert_restrictive(guarded_delete);
    assert_no_private_download_leak(guarded_delete, storage_root);
    assert(get_download(client, headers, first_id).status == 200);

    const auto deleted_after_download = delete_bundle(client, headers, first_id);
    assert(deleted_after_download.status == 204 && deleted_after_download.body.empty());
    assert_restrictive(deleted_after_download);
    const auto deleted_status = get_status(client, headers, first_id);
    assert(deleted_status.status == 200 && !response_json(deleted_status)["download_available"]);
    assert_no_private_download_metadata(response_json(deleted_status));
    const auto deleted_download = get_download(client, headers, first_id);
    assert(deleted_download.status == 409 && response_json(deleted_download)["error"] == "no_download");
    assert_restrictive(deleted_download);
    assert_no_private_download_leak(deleted_download, storage_root);
    const auto repeated_delete = delete_bundle(client, headers, first_id);
    assert(repeated_delete.status == 204 && repeated_delete.body.empty());
    assert_restrictive(repeated_delete);

    executor->reset();
    const auto probe_created = require_response(client.Post("/api/support-bundles", headers, "{\"probe_i2c\":true}", "application/json"));
    assert(probe_created.status == 202);
    const std::string probe_id = response_json(probe_created)["id"];
    executor->wait_entered();
    executor->release(FakeExecutor::Mode::success);
    wait_for_state(client, headers, probe_id, "succeeded");
    const auto probe_status = response_json(get_status(client, headers, probe_id));
    assert(probe_status["i2c_probe_status"] == "succeeded");
    assert(probe_status["download_available"] == true);
    assert_no_private_download_metadata(probe_status);
    const auto deleted_without_download = delete_bundle(client, headers, probe_id);
    assert(deleted_without_download.status == 204);
    assert(!response_json(get_status(client, headers, probe_id))["download_available"]);
    const auto probe_download_after_delete = get_download(client, headers, probe_id);
    assert(probe_download_after_delete.status == 409 &&
           response_json(probe_download_after_delete)["error"] == "no_download");

    executor->reset();
    const auto failed_created = require_response(client.Post("/api/support-bundles", headers, "{}", "application/json"));
    const std::string failed_id = response_json(failed_created)["id"];
    executor->wait_entered();
    executor->release(FakeExecutor::Mode::failure);
    wait_for_state(client, headers, failed_id, "failed");
    const auto failed = get_status(client, headers, failed_id);
    const auto failed_body = response_json(failed);
    assert(failed_body["failure_category"] == "collector_failed");
    assert(failed.body.find("/secret") == std::string::npos);
    assert(failed.body.find(storage_root.string()) == std::string::npos);
    assert(!failed_body["download_available"]);
    assert_no_private_download_metadata(failed_body);
    const auto failed_download = get_download(client, headers, failed_id);
    assert(failed_download.status == 409);
    assert(response_json(failed_download)["error"] == "no_download");
    assert_restrictive(failed_download);
    assert_no_private_download_leak(failed_download, storage_root);
    const auto failed_delete = delete_bundle(client, headers, failed_id);
    assert(failed_delete.status == 409 && response_json(failed_delete)["error"] == "no_download");
    assert_restrictive(failed_delete);
    assert_no_private_download_leak(failed_delete, storage_root);

    const auto complete_corrupt_job = [&](FakeExecutor::Mode mode,
                                          int expected_status,
                                          const char *expected_error) {
        executor->reset();
        const auto created_corrupt = require_response(
            client.Post("/api/support-bundles", headers, "{}", "application/json"));
        assert(created_corrupt.status == 202);
        const std::string corrupt_id = response_json(created_corrupt)["id"];
        executor->wait_entered();
        executor->release(mode);
        wait_for_state(client, headers, corrupt_id, "succeeded");
        const auto corrupt_download = get_download(client, headers, corrupt_id);
        assert(corrupt_download.status == expected_status);
        assert(response_json(corrupt_download)["error"] == expected_error);
        assert_restrictive(corrupt_download);
        assert_no_private_download_leak(corrupt_download, storage_root);
    };
    complete_corrupt_job(FakeExecutor::Mode::corrupt_archive, 409, "artifact_corrupt");
    complete_corrupt_job(FakeExecutor::Mode::corrupt_sidecar, 409, "artifact_corrupt");
    complete_corrupt_job(FakeExecutor::Mode::unsafe_sidecar, 409, "artifact_unsafe");
    complete_corrupt_job(FakeExecutor::Mode::missing_archive, 503, "artifact_unavailable");

    executor->reset();
    const auto cleanup_created = require_response(
        client.Post("/api/support-bundles", headers, "{}", "application/json"));
    const std::string cleanup_id = response_json(cleanup_created)["id"];
    executor->wait_entered();
    executor->release(FakeExecutor::Mode::success);
    wait_for_state(client, headers, cleanup_id, "succeeded");
    fail_cleanup->store(true);
    const auto cleanup_failed = delete_bundle(client, headers, cleanup_id);
    assert(cleanup_failed.status == 503 && response_json(cleanup_failed)["error"] == "cleanup_failed");
    assert_restrictive(cleanup_failed);
    assert_no_private_download_leak(cleanup_failed, storage_root);
    assert(get_download(client, headers, cleanup_id).status == 200);
    fail_cleanup->store(false);
    assert(delete_bundle(client, headers, cleanup_id).status == 204);

    executor->reset();
    const std::string private_body = R"({"probe_i2c":false,"support_context":{"kind":"existing_github_issue","issue_url":"https://github.com/WsprryPi/WsprryPi/issues/414"}})";
    const auto private_created = require_response(client.Post(
        "/api/support-bundles", headers, private_body, "application/json"));
    assert(private_created.status == 202);
    const auto private_created_body = response_json(private_created);
    const std::string private_id = private_created_body["id"];
    assert(private_created_body["case_id"] == "7K3M-9QFX-2DPA");
    assert(private_created_body["workflow_state"] == "collecting");
    assert(private_created.body.find("issues/414") == std::string::npos);
    executor->wait_entered();
    executor->release(FakeExecutor::Mode::success);
    wait_for_state(client, headers, private_id, "succeeded");
    assert(response_json(get_status(client, headers, private_id))["workflow_state"] ==
           "candidate_ready");
    const auto premature_finalize = finalize_bundle(client, headers, private_id);
    assert(premature_finalize.status == 409 &&
           response_json(premature_finalize)["error"] == "download_required");
    const auto guarded_finalize = finalize_bundle(client, foreign_origin, private_id);
    assert(guarded_finalize.status == 403);
    const auto body_finalize = require_response(client.Post(
        "/api/support-bundles/" + private_id + "/finalize", headers, "{}",
        "application/json"));
    assert(body_finalize.status == 400);
    assert(get_download(client, headers, private_id).status == 200);
    wait_for_workflow_state(
        client, headers, private_id, "candidate_downloaded");
    const auto finalized = finalize_bundle(client, headers, private_id);
    assert(finalized.status == 200);
    assert(response_json(finalized)["workflow_state"] == "finalized");
    assert(finalized.body.find("issues/414") == std::string::npos);
    assert(finalize_bundle(client, headers, private_id).status == 200);
    struct stat finalized_info {};
    assert(lstat((storage_root / private_id / "WsprryPi-support-test.tar.gz").c_str(),
                 &finalized_info) == 0);
    assert((finalized_info.st_mode & 0777) == 0400);
    assert(get_download(client, headers, private_id).status == 200);

    const auto encrypted = manager.encrypt_candidate(
        private_id, std::string(kSupportBundleProductionKeyId),
        std::string(kSupportBundleProductionAgeRecipient), fake_age);
    assert(encrypted.status == SupportBundleEncryptionStatus::encrypted);
    const auto encrypted_download = require_response(client.Get(
        "/api/support-bundles/" + private_id + "/encrypted", headers));
    assert(encrypted_download.status == 200 && !encrypted_download.body.empty());
    wait_for_workflow_state(
        client, headers, private_id, "encrypted_downloaded");
    assert(manager.receipt_reference(private_id).status ==
           SupportBundleDownloadReferenceStatus::available);
    const std::string upload_report_path =
        "/api/support-bundles/" + private_id + "/upload-reported-complete";
    const auto premature_upload_report =
        require_response(client.Post(upload_report_path, headers));
    assert(premature_upload_report.status == 409 &&
           response_json(premature_upload_report)["error"] == "upload_page_not_opened");

    intake_result = {};
    intake_result.status = SupportBundleIntakeProductionStatus::active;
    intake_result.generation = 9;
    intake_result.expires_at = "2026-09-03T00:00:00Z";
    intake_result.minimum_upload_version = "1.2.3";
    intake_result.signing_key_id = "wsprrypi-intake-2026-01";
    intake_result.bundle_key_id = "wsprrypi-bundle-2026-01";
    intake_result.request_url = "https://www.dropbox.com/request/fresh-capability_9";
    const int successful_handoff_start = intake_calls;
    const std::string handoff_path =
        "/api/support-bundles/" + private_id + "/handoff";
    const auto handoff = require_response(client.Get(handoff_path, headers));
    assert(handoff.status == 302 && intake_calls == successful_handoff_start + 1);
    assert(handoff.get_header_value("Location") == *intake_result.request_url);
    assert(handoff.get_header_value("Cache-Control") == "no-store");
    assert(handoff.get_header_value("X-Content-Type-Options") == "nosniff");
    assert(handoff.get_header_value("Referrer-Policy") == "no-referrer");
    assert_restrictive(handoff);
    assert(response_json(get_status(client, headers, private_id))["workflow_state"] ==
           "upload_page_opened");

    const auto upload_report_body = require_response(client.Post(
        upload_report_path, headers, "{}", "application/json"));
    assert(upload_report_body.status == 400);
    const auto guarded_upload_report =
        require_response(client.Post(upload_report_path, foreign_origin));
    assert(guarded_upload_report.status == 403);
    const auto upload_report = require_response(client.Post(upload_report_path, headers));
    assert(upload_report.status == 200 &&
           response_json(upload_report)["workflow_state"] == "upload_reported_complete");
    assert(response_json(get_status(client, headers, private_id))["workflow_state"] ==
           "upload_reported_complete");
    assert(require_response(client.Post(upload_report_path, headers)).status == 200);
    const auto retained_receipt = manager.receipt_reference(private_id);
    std::ifstream receipt_input(retained_receipt.archive_path);
    const nlohmann::json receipt_json = nlohmann::json::parse(receipt_input);
    assert(receipt_json["upload_state"] == "encrypted_artifact_downloaded");
    assert(require_response(client.Get(handoff_path, headers)).status == 302);
    assert(response_json(get_status(client, headers, private_id))["workflow_state"] ==
           "upload_reported_complete");

    intake_result.request_url = "https://www.dropbox.com/request/leak-me?unsafe=1";
    const auto malformed_handoff = require_response(client.Get(handoff_path, headers));
    assert(malformed_handoff.status == 409 && !malformed_handoff.has_header("Location"));
    assert(malformed_handoff.body.find("leak-me") == std::string::npos);

    intake_result = {};
    intake_result.status = SupportBundleIntakeProductionStatus::disabled;
    intake_result.generation = 10;
    intake_result.expires_at = "2026-09-04T00:00:00Z";
    intake_result.signing_key_id = "wsprrypi-intake-2026-01";
    intake_result.bundle_key_id = "wsprrypi-bundle-2026-01";
    const auto disabled_handoff = require_response(client.Get(handoff_path, headers));
    assert(disabled_handoff.status == 409 && !disabled_handoff.has_header("Location"));

    intake_throw = true;
    const auto failed_handoff = require_response(client.Get(handoff_path, headers));
    assert(failed_handoff.status == 503 && !failed_handoff.has_header("Location"));
    assert(failed_handoff.get_header_value("Referrer-Policy") == "no-referrer");
    intake_throw = false;
    assert(delete_bundle(client, headers, private_id).status == 204);

    executor->reset();
    const std::string no_github_description = "intermittent schedule failure";
    const std::string no_github_contact = "radio@example.test";
    const std::string no_github_body =
        "{\"support_context\":{\"kind\":\"no_github\",\"problem_description\":\"" +
        no_github_description + "\",\"contact\":\"" + no_github_contact + "\"}}";
    const auto no_github_created = require_response(client.Post(
        "/api/support-bundles", headers, no_github_body, "application/json"));
    assert(no_github_created.status == 202);
    assert(no_github_created.body.find(no_github_description) == std::string::npos);
    assert(no_github_created.body.find(no_github_contact) == std::string::npos);
    const std::string no_github_id = response_json(no_github_created)["id"];
    executor->wait_entered();
    executor->release(FakeExecutor::Mode::success);
    wait_for_state(client, headers, no_github_id, "succeeded");
    assert(get_download(client, headers, no_github_id).status == 200);
    std::ofstream(storage_root / no_github_id / "WsprryPi-support-test.tar.gz",
                  std::ios::app) << "size-change";
    const auto mutated_finalize = finalize_bundle(client, headers, no_github_id);
    assert(mutated_finalize.status == 409 &&
           response_json(mutated_finalize)["error"] == "artifact_invalid");
    assert(mutated_finalize.body.find(no_github_description) == std::string::npos);
    assert(mutated_finalize.body.find(no_github_contact) == std::string::npos);
    assert(delete_bundle(client, headers, no_github_id).status == 204);

    const std::vector<std::string> invalid_private_requests = {
        R"({"support_context":{"kind":"existing_github_issue","issue_url":"https://github.com/WsprryPi/WsprryPi/issues/414","contact":"leak"}})",
        R"({"support_context":{"kind":"new_github_issue","description":"missing contract name","contact":"radio@example.test"}})",
        R"({"support_context":{"kind":"no_github","problem_description":"description only"}})",
    };
    for (const auto &body : invalid_private_requests) {
        const auto invalid_private = require_response(
            client.Post("/api/support-bundles", headers, body, "application/json"));
        assert(invalid_private.status == 400);
    }

    const auto malformed = require_response(client.Get("/api/support-bundles/not-an-id", headers));
    assert(malformed.status == 404);
    assert_restrictive(malformed);
    const auto unknown = require_response(client.Get("/api/support-bundles/" + job_id('z'), headers));
    assert(unknown.status == 404);
    assert_restrictive(unknown);
    const auto malformed_upload_report = require_response(
        client.Post("/api/support-bundles/not-an-id/upload-reported-complete", headers));
    assert(malformed_upload_report.status == 404);
    const auto unknown_upload_report = require_response(client.Post(
        "/api/support-bundles/" + job_id('z') + "/upload-reported-complete", headers));
    assert(unknown_upload_report.status == 404);
    const auto malformed_download = require_response(
        client.Get("/api/support-bundles/not-an-id/download", headers));
    assert(malformed_download.status == 404);
    assert_restrictive(malformed_download);
    assert_no_private_download_leak(malformed_download, storage_root);
    const auto unknown_download = get_download(client, headers, job_id('z'));
    assert(unknown_download.status == 404);
    assert_restrictive(unknown_download);
    assert_no_private_download_leak(unknown_download, storage_root);
    const auto malformed_delete = delete_bundle(client, headers, "not-an-id");
    assert(malformed_delete.status == 404);
    assert_restrictive(malformed_delete);
    assert_no_private_download_leak(malformed_delete, storage_root);
    const auto unknown_delete = delete_bundle(client, headers, job_id('z'));
    assert(unknown_delete.status == 404);
    assert_restrictive(unknown_delete);
    assert_no_private_download_leak(unknown_delete, storage_root);
    const auto listing = require_response(client.Get("/api/support-bundles", headers));
    assert(listing.status == 404);
    assert_restrictive(listing);

    const auto foreign = require_response(client.Post("/api/support-bundles", foreign_origin, "{}", "application/json"));
    assert(foreign.status == 403);
    assert_restrictive(foreign);
    const auto foreign_download = require_response(
        client.Get("/api/support-bundles/" + first_id + "/download", foreign_origin));
    assert(foreign_download.status == 403);
    assert_restrictive(foreign_download);
    assert_no_private_download_leak(foreign_download, storage_root);
    const auto foreign_delete = delete_bundle(client, foreign_origin, first_id);
    assert(foreign_delete.status == 403);
    assert_restrictive(foreign_delete);
    assert_no_private_download_leak(foreign_delete, storage_root);
    const httplib::Headers null_origin{{"Host", "127.0.0.1:" + std::to_string(port)}, {"Origin", "null"}};
    assert(require_response(client.Post("/api/support-bundles", null_origin, "{}", "application/json")).status == 403);
    assert(require_response(client.Get("/api/support-bundles/" + first_id + "/download", null_origin)).status == 403);
    assert(delete_bundle(client, null_origin, first_id).status == 403);
    const httplib::Headers bad_host{{"Host", "example.invalid"}, {"X-Forwarded-For", "127.0.0.1"}};
    assert(require_response(client.Post("/api/support-bundles", bad_host, "{}", "application/json")).status == 403);
    const auto forwarded_download = require_response(
        client.Get("/api/support-bundles/" + first_id + "/download", bad_host));
    assert(forwarded_download.status == 403);
    assert_restrictive(forwarded_download);
    const auto forwarded_delete = delete_bundle(client, bad_host, first_id);
    assert(forwarded_delete.status == 403);
    assert_restrictive(forwarded_delete);
    const auto preflight = require_response(client.Options("/api/support-bundles", foreign_origin));
    assert(preflight.status == 403);
    assert_restrictive(preflight);
    const auto download_preflight = require_response(
        client.Options("/api/support-bundles/" + first_id + "/download", foreign_origin));
    assert(download_preflight.status == 403);
    assert_restrictive(download_preflight);
    const auto local_download_options = require_response(
        client.Options("/api/support-bundles/" + first_id + "/download", headers));
    assert(local_download_options.status == 204);
    assert_restrictive(local_download_options);
    const auto delete_preflight = require_response(
        client.Options("/api/support-bundles/" + first_id, foreign_origin));
    assert(delete_preflight.status == 403);
    assert_restrictive(delete_preflight);
    const auto local_delete_options = require_response(
        client.Options("/api/support-bundles/" + first_id, headers));
    assert(local_delete_options.status == 204);
    assert(local_delete_options.get_header_value("Allow") == "GET, DELETE, OPTIONS");
    assert_restrictive(local_delete_options);

    server.stop();
    server_thread.join();
    manager.shutdown();
    fs::remove_all(storage_root);
    std::cout << "support_bundle_http_test: PASS\n";
}
