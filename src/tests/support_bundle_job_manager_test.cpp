#include "support_bundle_job_manager.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "json.hpp"
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

class FakeExecutor final : public SupportBundleJobExecutor {
public:
    enum class ResultMode { valid, none, multiple, symlink, oversized, malformed, schema_invalid, inconsistent };
    std::mutex mutex; std::condition_variable cv; bool entered=false, released=false, cancelled=false, fail=false, throw_exception=false; bool probe=false; int calls=0;
    ResultMode mode = ResultMode::valid;
    std::filesystem::path directory;
    std::filesystem::path symlink_target;
    std::string case_id;
    std::string digest = std::string(64, 'A');
    bool manifest_included = false;
    SupportBundleExecutionResult run(const SupportBundleExecutionContext &context) override {
        const bool requested = context.probe_i2c; directory = context.job_directory;
        std::unique_lock lock(mutex); ++calls; probe=requested; entered=true; cv.notify_all();
        cv.wait(lock, [&] { return released || cancelled; });
        if (throw_exception) throw 1;
        if (cancelled) return {true, {}, {}}; // Manager must preserve cancellation over this late success.
        if (!fail && !throw_exception && mode != ResultMode::none) {
            nlohmann::json result={{"schema_version",1},{"status","success"},{"archive_filename","WsprryPi-support-test.tar.gz"},{"sha256_filename","WsprryPi-support-test.tar.gz.sha256"},{"sha256",digest},{"generated_at_utc","20260101T000000Z"},{"configuration_files_included",true},{"full_logs_included",false},{"i2c_probe_requested",probe},{"i2c_probe_status",probe?"succeeded":"skipped_by_user"},{"privileged_diagnostics_may_be_incomplete",false}};
            if (!case_id.empty() || manifest_included) {
                result["case_id"] = case_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(case_id);
                result["manifest_included"] = manifest_included;
            }
            if (mode == ResultMode::schema_invalid) result["schema_version"] = 2;
            if (mode == ResultMode::inconsistent) result["i2c_probe_status"] = probe ? "skipped_by_user" : "succeeded";
            const auto path = context.job_directory / "WsprryPi-support-test.tar.gz.result.json";
            if (mode == ResultMode::symlink) {
                const auto target = symlink_target.empty() ? std::filesystem::path("/tmp") : symlink_target;
                symlink(target.c_str(), path.c_str());
            }
            else if (mode == ResultMode::oversized) std::ofstream(path) << std::string(65537, 'x');
            else if (mode == ResultMode::malformed) std::ofstream(path) << "{";
            else { std::ofstream(path) << result.dump(); if (mode == ResultMode::multiple) std::ofstream(context.job_directory / "WsprryPi-support-two.tar.gz.result.json") << result.dump(); }
        }
        return fail ? SupportBundleExecutionResult{false, "raw:/secret", "/secret/token"} : SupportBundleExecutionResult{true, {}, {}};
    }
    void request_stop() noexcept override { std::lock_guard lock(mutex); cancelled=true; cv.notify_all(); }
    void wait_entered() { std::unique_lock lock(mutex); assert(cv.wait_for(lock, std::chrono::seconds(2), [&] { return entered; })); }
    void release() { std::lock_guard lock(mutex); released=true; cv.notify_all(); }
    void reset() { std::lock_guard lock(mutex); entered=false; released=false; cancelled=false; }
    bool was_cancelled() { std::lock_guard lock(mutex); return cancelled; }
};

static void wait_terminal(SupportBundleJobManager &manager, const std::string &id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto state = manager.lookup(id)->state;
        if (state == SupportBundleJobState::succeeded || state == SupportBundleJobState::failed) return;
        std::this_thread::yield();
    }
    assert(false);
}
static void wait_removed(const std::filesystem::path &path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!std::filesystem::exists(path)) return;
        std::this_thread::yield();
    }
    assert(false);
}
static void wait_for_condition(const std::function<bool()> &condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false);
}
static std::string id(char c) { return std::string(32, c); }
static void assert_no_download_metadata(const SupportBundleDownloadReference &reference) {
    assert(reference.archive_path.empty());
    assert(reference.archive_basename.empty());
    assert(reference.checksum_path.empty());
    assert(reference.checksum_basename.empty());
    assert(reference.expected_sha256.empty());
}
static void expect_storage_failure(const std::filesystem::path &root, const std::string &expected) {
    auto executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager manager(executor, [] { return id('z'); }, root);
    std::string error;
    assert(!manager.create({}, error) && error == expected && executor->calls == 0);
}
static void expect_result_failure(const std::filesystem::path &root, FakeExecutor::ResultMode mode, const std::string &category, char token) {
    auto executor = std::make_shared<FakeExecutor>(); executor->mode = mode;
    std::atomic<int> cleanup_result{-1};
    SupportBundleJobManager manager(
        executor, [token] { return id(token); }, root,
        [&cleanup_result](const std::filesystem::path &storage_root, const std::string &job_id) {
            const auto result = remove_support_bundle_job_directory(storage_root, job_id);
            cleanup_result.store(static_cast<int>(result.failure));
            return result;
        });
    std::string error;
    const auto job = manager.create({}, error); executor->wait_entered(); executor->release(); wait_terminal(manager, job->id);
    const auto snapshot = manager.lookup(job->id); assert(snapshot->state == SupportBundleJobState::failed && snapshot->failure_category == category && snapshot->failure_message == "Support collection failed." && snapshot->i2c_probe_status.empty() && !snapshot->download_available);
    const auto reference = manager.download_reference(job->id);
    assert(reference.status == SupportBundleDownloadReferenceStatus::no_download);
    assert_no_download_metadata(reference);
    wait_removed(root / job->id);
    const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (cleanup_result.load() == -1 && std::chrono::steady_clock::now() < cleanup_deadline) {
        std::this_thread::yield();
    }
    assert(cleanup_result.load() ==
           static_cast<int>(SupportBundleJobDirectoryRemovalFailure::none));
    manager.shutdown();
}

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "--encrypt") {
        std::ifstream input(argv[6], std::ios::binary);
        std::ofstream output(argv[5], std::ios::binary);
        output << "encrypted:" << input.rdbuf();
        output.close();
        return chmod(argv[5], 0600) == 0 ? 0 : 1;
    }
    char template_path[] = "/tmp/wsprrypi-job-manager-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const std::filesystem::path root = std::filesystem::canonical(template_path);
    assert(chmod(root.c_str(), 0700) == 0);
    const auto fake_age = root / "fake-age";
    assert(std::filesystem::copy_file(std::filesystem::canonical(argv[0]), fake_age));
    assert(chmod(fake_age.c_str(), 0500) == 0);
    const auto sibling = root / id('p');
    assert(std::filesystem::create_directory(sibling));
    assert(chmod(sibling.c_str(), 0700) == 0);
    { std::ofstream sentinel(sibling / "keep"); sentinel << "sentinel"; }
    expect_storage_failure("relative-root", "storage_unavailable");
    expect_storage_failure(root / "missing", "storage_unavailable");
    const auto link_root = root / "root-link"; assert(symlink(root.c_str(), link_root.c_str()) == 0); expect_storage_failure(link_root, "storage_unavailable");
    for (const mode_t mode : {0750, 0070, 0770, 0777}) { const auto invalid = root / ("mode-" + std::to_string(mode)); assert(std::filesystem::create_directory(invalid)); assert(chmod(invalid.c_str(), mode) == 0); expect_storage_failure(invalid, "storage_unavailable"); assert(chmod(invalid.c_str(), 0700) == 0); }
    std::string error;
    auto success = std::make_shared<FakeExecutor>();
    success->case_id = "7K3M-9QFX-2DPA";
    success->manifest_included = true;
    int generated = 0; SupportBundleJobManager manager(
        success, [&] { return id(generated++ == 0 ? 'a' : 'g'); }, root,
        remove_support_bundle_job_directory,
        SupportBundleJobManager::kProductionRetention,
        SupportBundleJobManager::kProductionRetryDelay,
        [] { return std::optional<std::string>("7K3M-9QFX-2DPA"); });
    SupportBundleContext issue_context;
    issue_context.kind = SupportBundleContextKind::existing_github_issue;
    issue_context.issue_url = "https://github.com/WsprryPi/WsprryPi/issues/414";
    const auto first = manager.create({true, SupportBundlePrivateRequest{issue_context}}, error); assert(first && error.empty() && !first->download_available && first->case_id == "7K3M-9QFX-2DPA" && first->private_lifecycle == SupportBundlePrivateLifecycle::collecting);
    const auto queued_reference = manager.download_reference(first->id);
    assert(queued_reference.status == SupportBundleDownloadReferenceStatus::not_ready);
    assert_no_download_metadata(queued_reference);
    assert(manager.delete_download("bad").status ==
           SupportBundleDownloadDeletionStatus::malformed_or_unknown_id);
    assert(manager.delete_download(first->id).status ==
           SupportBundleDownloadDeletionStatus::not_terminal);
    success->wait_entered(); struct stat job_info{};
    assert(success->probe);
    assert(success->directory == root / first->id);
    assert(manager.lookup(first->id)->i2c_probe_status.empty());
    assert(lstat(success->directory.c_str(), &job_info) == 0);
    assert(S_ISDIR(job_info.st_mode));
    assert((job_info.st_mode & 0777) == 0700);
    assert(manager.lookup(first->id)->state == SupportBundleJobState::running);
    const auto running_reference = manager.download_reference(first->id);
    assert(running_reference.status == SupportBundleDownloadReferenceStatus::not_ready);
    assert_no_download_metadata(running_reference);
    assert(!manager.create({}, error) && error == "job_active"); assert(manager.lookup(first->id)); assert(!manager.lookup("bad"));
    success->release(); wait_terminal(manager, first->id); assert(manager.lookup(first->id)->state == SupportBundleJobState::succeeded && manager.lookup(first->id)->i2c_probe_status == "succeeded" && manager.lookup(first->id)->download_available && std::filesystem::exists(success->directory));
    const auto ready = manager.download_reference(first->id);
    assert(ready.status == SupportBundleDownloadReferenceStatus::available);
    assert(ready.archive_path == root / first->id / "WsprryPi-support-test.tar.gz");
    assert(ready.archive_basename == "WsprryPi-support-test.tar.gz");
    assert(ready.checksum_path == root / first->id / "WsprryPi-support-test.tar.gz.sha256");
    assert(ready.checksum_basename == "WsprryPi-support-test.tar.gz.sha256");
    assert(ready.expected_sha256 == std::string(64, 'a'));
    const auto public_snapshot = manager.lookup(first->id); assert(public_snapshot->failure_message.find("WsprryPi-support-test.tar.gz") == std::string::npos && public_snapshot->failure_message.find(root.string()) == std::string::npos && public_snapshot->i2c_probe_status == "succeeded" && public_snapshot->case_id == "7K3M-9QFX-2DPA" && public_snapshot->private_lifecycle == SupportBundlePrivateLifecycle::candidate_ready);
    assert(manager.delete_download(id('q')).status ==
           SupportBundleDownloadDeletionStatus::malformed_or_unknown_id);
    assert(manager.delete_download(first->id).status ==
           SupportBundleDownloadDeletionStatus::removed);
    const auto deleted_snapshot = manager.lookup(first->id);
    assert(deleted_snapshot->state == SupportBundleJobState::succeeded &&
           !deleted_snapshot->download_available &&
           deleted_snapshot->case_id.empty() &&
           deleted_snapshot->private_lifecycle == SupportBundlePrivateLifecycle::none &&
           deleted_snapshot->failure_category.empty() && deleted_snapshot->failure_message.empty() &&
           !std::filesystem::exists(root / first->id));
    const auto deleted_reference = manager.download_reference(first->id);
    assert(deleted_reference.status == SupportBundleDownloadReferenceStatus::no_download);
    assert_no_download_metadata(deleted_reference);
    assert(manager.delete_download(first->id).status ==
           SupportBundleDownloadDeletionStatus::already_removed);
    const auto malformed_reference = manager.download_reference("bad");
    assert(malformed_reference.status == SupportBundleDownloadReferenceStatus::malformed_or_unknown_id);
    assert_no_download_metadata(malformed_reference);
    const auto unknown_reference = manager.download_reference(id('q'));
    assert(unknown_reference.status == SupportBundleDownloadReferenceStatus::malformed_or_unknown_id);
    assert_no_download_metadata(unknown_reference);
    success->reset(); success->case_id.clear(); success->manifest_included = false; const auto next = manager.create({false}, error); assert(next); success->wait_entered(); assert(!success->probe); success->release(); wait_terminal(manager,next->id); assert(manager.lookup(next->id)->i2c_probe_status == "skipped_by_user" && manager.lookup(next->id)->download_available && manager.lookup(next->id)->case_id.empty() && manager.lookup(next->id)->private_lifecycle == SupportBundlePrivateLifecycle::none); manager.shutdown();

    const auto existing_dir = root / id('z'); assert(std::filesystem::create_directory(existing_dir)); expect_storage_failure(root, "job_setup_failed"); std::filesystem::remove(existing_dir);
    const auto existing_file = root / id('z'); { std::ofstream output(existing_file); output << "x"; } expect_storage_failure(root, "job_setup_failed"); std::filesystem::remove(existing_file);
    const auto existing_link = root / id('z'); assert(symlink(root.c_str(), existing_link.c_str()) == 0); expect_storage_failure(root, "job_setup_failed"); std::filesystem::remove(existing_link);

    expect_result_failure(root, FakeExecutor::ResultMode::none, "result_missing", 'h');
    expect_result_failure(root, FakeExecutor::ResultMode::multiple, "result_ambiguous", 'i');
    expect_result_failure(root, FakeExecutor::ResultMode::symlink, "result_unsafe", 'j');
    expect_result_failure(root, FakeExecutor::ResultMode::oversized, "result_oversized", 'k');
    expect_result_failure(root, FakeExecutor::ResultMode::malformed, "result_invalid", 'l');
    expect_result_failure(root, FakeExecutor::ResultMode::schema_invalid, "result_invalid", 'm');
    expect_result_failure(root, FakeExecutor::ResultMode::inconsistent, "result_inconsistent", 'n');
    assert(std::filesystem::exists(sibling / "keep"));

    char external_template[] = "/tmp/wsprrypi-job-manager-cleanup-external.XXXXXX";
    assert(mkdtemp(external_template) != nullptr);
    const std::filesystem::path external = external_template;
    assert(chmod(external.c_str(), 0700) == 0);
    { std::ofstream external_sentinel(external / "keep"); external_sentinel << "sentinel"; }
    auto symlink_failure = std::make_shared<FakeExecutor>();
    symlink_failure->mode = FakeExecutor::ResultMode::symlink;
    symlink_failure->symlink_target = external;
    SupportBundleJobManager external_safe(symlink_failure, [] { return id('s'); }, root);
    const auto external_job = external_safe.create({}, error);
    symlink_failure->wait_entered();
    symlink_failure->release();
    wait_terminal(external_safe, external_job->id);
    wait_removed(root / external_job->id);
    assert(std::filesystem::exists(external / "keep"));
    external_safe.shutdown();
    std::filesystem::remove_all(external);

    auto failing = std::make_shared<FakeExecutor>(); failing->fail=true;
    SupportBundleJobManager failed(failing, [] { return id('b'); }, root); auto f = failed.create({}, error); failing->wait_entered(); failing->release(); wait_terminal(failed, f->id); wait_removed(root / f->id); const auto fs=failed.lookup(f->id); assert(fs->state==SupportBundleJobState::failed && fs->failure_category=="collector_failed" && fs->failure_message=="Support collection failed." && !fs->download_available); const auto failed_reference = failed.download_reference(f->id); assert(failed_reference.status == SupportBundleDownloadReferenceStatus::no_download); assert_no_download_metadata(failed_reference); assert(failed.delete_download(f->id).status == SupportBundleDownloadDeletionStatus::no_retained_download); failed.shutdown();
    auto throwing = std::make_shared<FakeExecutor>(); throwing->throw_exception=true;
    SupportBundleJobManager exception(throwing, [] { return id('c'); }, root); auto e=exception.create({},error); throwing->wait_entered(); throwing->release(); wait_terminal(exception,e->id); wait_removed(root / e->id); assert(exception.lookup(e->id)->failure_category=="executor_exception"); exception.shutdown();
    auto malformed = std::make_shared<FakeExecutor>(); SupportBundleJobManager bad_id(malformed, [] { return "bad"; }, root); assert(!bad_id.create({},error) && error=="invalid_job_id" && malformed->calls==0);
    auto throwing_id = std::make_shared<FakeExecutor>(); SupportBundleJobManager id_error(throwing_id, []()->std::string { throw 1; }, root); assert(!id_error.create({},error) && error=="id_generation_failed" && throwing_id->calls==0);
    std::string high=id('d'); high[0]=static_cast<char>(0x80); assert(!SupportBundleJobManager::valid_id(high));
    auto cancelling = std::make_shared<FakeExecutor>(); SupportBundleJobManager cancel(cancelling, [] { return id('e'); }, root); auto c=cancel.create({},error); cancelling->wait_entered(); cancel.shutdown(); assert(cancelling->was_cancelled()); const auto cs=cancel.lookup(c->id); assert(cs->state==SupportBundleJobState::failed && cs->failure_category=="shutting_down" && !cs->download_available && !std::filesystem::exists(root / c->id)); const auto cancelled_reference = cancel.download_reference(c->id); assert(cancelled_reference.status == SupportBundleDownloadReferenceStatus::no_download); assert_no_download_metadata(cancelled_reference); assert(cancel.delete_download(c->id).status == SupportBundleDownloadDeletionStatus::no_retained_download); assert(!cancel.create({},error) && error=="shutting_down"); cancel.shutdown();
    std::string destroyed_id;
    { auto destructor_executor=std::make_shared<FakeExecutor>(); SupportBundleJobManager destruct(destructor_executor, [] { return id('f'); }, root); const auto d = destruct.create({},error); destroyed_id = d->id; destructor_executor->wait_entered(); }
    assert(!std::filesystem::exists(root / destroyed_id));
    std::atomic<int> cleanup_calls = 0;
    auto cleanup_failure_executor = std::make_shared<FakeExecutor>();
    cleanup_failure_executor->mode = FakeExecutor::ResultMode::none;
    SupportBundleJobManager cleanup_failure(
        cleanup_failure_executor, [] { return id('r'); }, root,
        [&cleanup_calls](const std::filesystem::path &, const std::string &) {
            ++cleanup_calls;
            return SupportBundleJobDirectoryRemovalResult{
                SupportBundleJobDirectoryRemovalFailure::removal_failed};
        });
    const auto cleanup_job = cleanup_failure.create({}, error);
    cleanup_failure_executor->wait_entered();
    cleanup_failure_executor->release();
    wait_terminal(cleanup_failure, cleanup_job->id);
    const auto cleanup_snapshot = cleanup_failure.lookup(cleanup_job->id);
    const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (cleanup_calls.load() == 0 && std::chrono::steady_clock::now() < cleanup_deadline) {
        std::this_thread::yield();
    }
    assert(cleanup_calls.load() == 1 && cleanup_snapshot->state == SupportBundleJobState::failed &&
           cleanup_snapshot->failure_category == "result_missing" &&
           cleanup_snapshot->failure_message == "Support collection failed." &&
           !cleanup_snapshot->download_available &&
           std::filesystem::exists(root / cleanup_job->id));
    cleanup_failure.shutdown();
    std::filesystem::remove_all(root / cleanup_job->id);

    std::atomic<int> delete_cleanup_calls = 0;
    auto delete_retry_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager delete_retry(
        delete_retry_executor, [] { return id('t'); }, root,
        [&delete_cleanup_calls](const std::filesystem::path &storage_root,
                                const std::string &job_id) {
            if (++delete_cleanup_calls == 1) {
                return SupportBundleJobDirectoryRemovalResult{
                    SupportBundleJobDirectoryRemovalFailure::removal_failed};
            }
            return remove_support_bundle_job_directory(storage_root, job_id);
        });
    const auto retry_job = delete_retry.create({}, error);
    delete_retry_executor->wait_entered();
    delete_retry_executor->release();
    wait_terminal(delete_retry, retry_job->id);
    assert(delete_retry.download_reference(retry_job->id).status ==
           SupportBundleDownloadReferenceStatus::available);
    assert(delete_retry.delete_download(retry_job->id).status ==
           SupportBundleDownloadDeletionStatus::cleanup_failed);
    assert(delete_retry.download_reference(retry_job->id).status ==
           SupportBundleDownloadReferenceStatus::available &&
           delete_retry.lookup(retry_job->id)->download_available &&
           std::filesystem::exists(root / retry_job->id));
    assert(delete_retry.delete_download(retry_job->id).status ==
           SupportBundleDownloadDeletionStatus::removed);
    assert(delete_cleanup_calls.load() == 2 &&
           delete_retry.download_reference(retry_job->id).status ==
               SupportBundleDownloadReferenceStatus::no_download);
    delete_retry.shutdown();

    auto concurrent_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager concurrent(concurrent_executor, [] { return id('u'); }, root);
    const auto concurrent_job = concurrent.create({}, error);
    concurrent_executor->wait_entered();
    concurrent_executor->release();
    wait_terminal(concurrent, concurrent_job->id);
    std::atomic<bool> reader_started = false;
    std::thread reader([&] {
        reader_started = true;
        for (int iteration = 0; iteration < 64; ++iteration) {
            assert(concurrent.lookup(concurrent_job->id));
            (void)concurrent.download_reference(concurrent_job->id);
        }
    });
    while (!reader_started.load()) std::this_thread::yield();
    assert(concurrent.delete_download(concurrent_job->id).status ==
           SupportBundleDownloadDeletionStatus::removed);
    reader.join();
    concurrent.shutdown();

    std::atomic<int> expiry_cleanup_calls = 0;
    auto expiry_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager expiry(
        expiry_executor, [] { return id('v'); }, root,
        [&expiry_cleanup_calls](const std::filesystem::path &storage_root,
                                const std::string &job_id) {
            ++expiry_cleanup_calls;
            return remove_support_bundle_job_directory(storage_root, job_id);
        }, std::chrono::milliseconds(40), std::chrono::milliseconds(20));
    const auto expiry_job = expiry.create({}, error);
    expiry_executor->wait_entered();
    expiry_executor->release();
    wait_terminal(expiry, expiry_job->id);
    assert(expiry.download_reference(expiry_job->id).status ==
           SupportBundleDownloadReferenceStatus::available);
    wait_for_condition([&] {
        return !expiry.lookup(expiry_job->id)->download_available;
    });
    assert(expiry_cleanup_calls.load() == 1 &&
           expiry.lookup(expiry_job->id)->state == SupportBundleJobState::succeeded &&
           expiry.download_reference(expiry_job->id).status ==
               SupportBundleDownloadReferenceStatus::no_download &&
           expiry.delete_download(expiry_job->id).status ==
               SupportBundleDownloadDeletionStatus::already_removed &&
           !std::filesystem::exists(root / expiry_job->id));
    expiry.shutdown();

    std::atomic<int> explicit_cleanup_calls = 0;
    auto explicit_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager explicit_delete(
        explicit_executor, [] { return id('w'); }, root,
        [&explicit_cleanup_calls](const std::filesystem::path &storage_root,
                                  const std::string &job_id) {
            ++explicit_cleanup_calls;
            return remove_support_bundle_job_directory(storage_root, job_id);
        }, std::chrono::milliseconds(80), std::chrono::milliseconds(20));
    const auto explicit_job = explicit_delete.create({}, error);
    explicit_executor->wait_entered();
    explicit_executor->release();
    wait_terminal(explicit_delete, explicit_job->id);
    assert(explicit_delete.delete_download(explicit_job->id).status ==
           SupportBundleDownloadDeletionStatus::removed);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    assert(explicit_cleanup_calls.load() == 1 &&
           explicit_delete.download_reference(explicit_job->id).status ==
               SupportBundleDownloadReferenceStatus::no_download);
    explicit_delete.shutdown();

    std::atomic<int> retry_cleanup_calls = 0;
    auto retry_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager retry_expiry(
        retry_executor, [] { return id('x'); }, root,
        [&retry_cleanup_calls](const std::filesystem::path &storage_root,
                               const std::string &job_id) {
            if (++retry_cleanup_calls == 1) {
                return SupportBundleJobDirectoryRemovalResult{
                    SupportBundleJobDirectoryRemovalFailure::removal_failed};
            }
            return remove_support_bundle_job_directory(storage_root, job_id);
        }, std::chrono::milliseconds(20), std::chrono::milliseconds(120));
    const auto retry_expiry_job = retry_expiry.create({}, error);
    retry_executor->wait_entered();
    retry_executor->release();
    wait_terminal(retry_expiry, retry_expiry_job->id);
    wait_for_condition([&] { return retry_cleanup_calls.load() == 1; });
    assert(retry_expiry.lookup(retry_expiry_job->id)->download_available &&
           retry_expiry.download_reference(retry_expiry_job->id).status ==
               SupportBundleDownloadReferenceStatus::available);
    wait_for_condition([&] {
        return !retry_expiry.lookup(retry_expiry_job->id)->download_available;
    });
    assert(retry_cleanup_calls.load() == 2 &&
           retry_expiry.download_reference(retry_expiry_job->id).status ==
               SupportBundleDownloadReferenceStatus::no_download);
    retry_expiry.shutdown();

    const auto make_private_executor = [] {
        auto executor = std::make_shared<FakeExecutor>();
        executor->case_id = "7K3M-9QFX-2DPA";
        executor->manifest_included = true;
        executor->digest =
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        return executor;
    };
    SupportBundleContext lifecycle_context;
    lifecycle_context.kind = SupportBundleContextKind::no_github;
    lifecycle_context.problem_description = "schedule stopped";
    lifecycle_context.contact = "radio@example.test";

    auto lifecycle_executor = make_private_executor();
    SupportBundleJobManager lifecycle(
        lifecycle_executor, [] { return id('4'); }, root,
        remove_support_bundle_job_directory, std::chrono::hours(1),
        std::chrono::milliseconds(20),
        [] { return std::optional<std::string>("7K3M-9QFX-2DPA"); });
    const auto lifecycle_job = lifecycle.create(
        {false, SupportBundlePrivateRequest{lifecycle_context}}, error);
    lifecycle_executor->wait_entered();
    lifecycle_executor->release();
    wait_terminal(lifecycle, lifecycle_job->id);
    const auto lifecycle_archive = root / lifecycle_job->id /
                                   "WsprryPi-support-test.tar.gz";
    { std::ofstream output(lifecycle_archive); output << "abc"; }
    assert(chmod(lifecycle_archive.c_str(), 0600) == 0);
    assert(lifecycle.lookup(lifecycle_job->id)->private_lifecycle ==
           SupportBundlePrivateLifecycle::candidate_ready);
    assert(lifecycle.finalize_candidate(lifecycle_job->id).status ==
           SupportBundleFinalizationStatus::download_required);
    assert(lifecycle.mark_candidate_downloaded(lifecycle_job->id, 4) ==
           SupportBundleCandidateDownloadStatus::marked);
    assert(lifecycle.finalize_candidate(lifecycle_job->id).status ==
           SupportBundleFinalizationStatus::artifact_invalid);
    assert(lifecycle.lookup(lifecycle_job->id)->private_lifecycle ==
           SupportBundlePrivateLifecycle::candidate_downloaded);
    assert(lifecycle.delete_download(lifecycle_job->id).status ==
           SupportBundleDownloadDeletionStatus::removed);
    lifecycle.shutdown();

    auto finalized_executor = make_private_executor();
    SupportBundleJobManager finalized(
        finalized_executor, [] { return id('5'); }, root,
        remove_support_bundle_job_directory, std::chrono::hours(1),
        std::chrono::milliseconds(20),
        [] { return std::optional<std::string>("7K3M-9QFX-2DPA"); });
    const auto finalized_job = finalized.create(
        {false, SupportBundlePrivateRequest{lifecycle_context}}, error);
    finalized_executor->wait_entered(); finalized_executor->release();
    wait_terminal(finalized, finalized_job->id);
    const auto finalized_archive = root / finalized_job->id /
                                   "WsprryPi-support-test.tar.gz";
    { std::ofstream output(finalized_archive); output << "def"; }
    assert(chmod(finalized_archive.c_str(), 0600) == 0);
    assert(finalized.mark_candidate_downloaded(finalized_job->id, 3) ==
           SupportBundleCandidateDownloadStatus::marked);
    assert(finalized.mark_candidate_downloaded(finalized_job->id, 3) ==
           SupportBundleCandidateDownloadStatus::already_marked);
    assert(finalized.finalize_candidate(finalized_job->id).status ==
           SupportBundleFinalizationStatus::artifact_invalid);
    { std::ofstream output(finalized_archive, std::ios::trunc); output << "abc"; }
    assert(finalized.finalize_candidate(finalized_job->id).status ==
           SupportBundleFinalizationStatus::finalized);
    assert(finalized.finalize_candidate(finalized_job->id).status ==
           SupportBundleFinalizationStatus::already_finalized);
    struct stat immutable_info {};
    assert(lstat(finalized_archive.c_str(), &immutable_info) == 0 &&
           (immutable_info.st_mode & 0777) == 0400);
    const auto encrypted_outcome = finalized.encrypt_candidate(
        finalized_job->id, "wsprrypi-bundle-2026-01", "age1test-recipient", fake_age);
    if (encrypted_outcome.status != SupportBundleEncryptionStatus::encrypted)
        std::cerr << "primitive failure=" << static_cast<int>(encrypted_outcome.primitive_failure) << "\n";
    assert(encrypted_outcome.status == SupportBundleEncryptionStatus::encrypted);
    assert(finalized.encrypt_candidate(finalized_job->id, "wsprrypi-bundle-2026-01",
                                       "age1test-recipient", fake_age).status ==
           SupportBundleEncryptionStatus::already_encrypted);
    const auto encrypted = finalized.encrypted_reference(finalized_job->id);
    assert(encrypted.status == SupportBundleDownloadReferenceStatus::available);
    struct stat encrypted_info{};
    assert(lstat(encrypted.archive_path.c_str(), &encrypted_info) == 0 &&
           (encrypted_info.st_mode & 0777) == 0600);
    assert(finalized.receipt_reference(finalized_job->id).status ==
           SupportBundleDownloadReferenceStatus::not_ready);
    assert(finalized.mark_upload_page_opened("bad") ==
           SupportBundleUploadTransitionStatus::malformed_or_unknown_id);
    assert(finalized.report_upload_complete("bad") ==
           SupportBundleUploadTransitionStatus::malformed_or_unknown_id);
    assert(finalized.mark_upload_page_opened(finalized_job->id) ==
           SupportBundleUploadTransitionStatus::unavailable);
    assert(finalized.report_upload_complete(finalized_job->id) ==
           SupportBundleUploadTransitionStatus::unavailable);
    assert(finalized.mark_encrypted_downloaded(finalized_job->id, encrypted_info.st_size) ==
           SupportBundleCandidateDownloadStatus::marked);
    assert(lstat(encrypted.archive_path.c_str(), &encrypted_info) == 0 &&
           (encrypted_info.st_mode & 0777) == 0400);
    assert(finalized.receipt_reference(finalized_job->id).status ==
           SupportBundleDownloadReferenceStatus::available);
    assert(finalized.lookup(finalized_job->id)->private_lifecycle ==
           SupportBundlePrivateLifecycle::encrypted_downloaded);
    assert(finalized.mark_upload_page_opened(finalized_job->id) ==
           SupportBundleUploadTransitionStatus::transitioned);
    assert(finalized.mark_upload_page_opened(finalized_job->id) ==
           SupportBundleUploadTransitionStatus::already_transitioned);
    assert(finalized.lookup(finalized_job->id)->private_lifecycle ==
           SupportBundlePrivateLifecycle::upload_page_opened);
    assert(finalized.report_upload_complete(finalized_job->id) ==
           SupportBundleUploadTransitionStatus::transitioned);
    assert(finalized.report_upload_complete(finalized_job->id) ==
           SupportBundleUploadTransitionStatus::already_transitioned);
    assert(finalized.lookup(finalized_job->id)->private_lifecycle ==
           SupportBundlePrivateLifecycle::upload_reported_complete);
    assert(finalized.finalize_candidate(finalized_job->id).status ==
           SupportBundleFinalizationStatus::already_finalized);
    assert(finalized.delete_download(finalized_job->id).status ==
           SupportBundleDownloadDeletionStatus::removed);
    finalized.shutdown();

    auto private_expiry_executor = make_private_executor();
    SupportBundleJobManager private_expiry(
        private_expiry_executor, [] { return id('6'); }, root,
        remove_support_bundle_job_directory, std::chrono::milliseconds(30),
        std::chrono::milliseconds(20),
        [] { return std::optional<std::string>("7K3M-9QFX-2DPA"); });
    const auto private_expiry_job = private_expiry.create(
        {false, SupportBundlePrivateRequest{lifecycle_context}}, error);
    private_expiry_executor->wait_entered(); private_expiry_executor->release();
    wait_terminal(private_expiry, private_expiry_job->id);
    wait_for_condition([&] {
        return !private_expiry.lookup(private_expiry_job->id)->download_available;
    });
    assert(private_expiry.lookup(private_expiry_job->id)->private_lifecycle ==
           SupportBundlePrivateLifecycle::none);
    private_expiry.shutdown();

    std::atomic<int> active_cleanup_calls = 0;
    auto active_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager active_expiry(
        active_executor, [] { return id('y'); }, root,
        [&active_cleanup_calls](const std::filesystem::path &storage_root,
                                const std::string &job_id) {
            ++active_cleanup_calls;
            return remove_support_bundle_job_directory(storage_root, job_id);
        }, std::chrono::milliseconds::zero(), std::chrono::milliseconds(20));
    const auto active_job = active_expiry.create({}, error);
    active_executor->wait_entered();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(active_cleanup_calls.load() == 0 &&
           active_expiry.lookup(active_job->id)->state == SupportBundleJobState::running);
    active_executor->release();
    wait_terminal(active_expiry, active_job->id);
    wait_for_condition([&] {
        return !active_expiry.lookup(active_job->id)->download_available;
    });
    assert(active_cleanup_calls.load() == 1);
    active_expiry.shutdown();

    SupportBundleContext invalid_context;
    invalid_context.kind = SupportBundleContextKind::no_github;
    invalid_context.problem_description = "missing contact";
    auto validation_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager context_validation(
        validation_executor, [] { return id('2'); }, root,
        remove_support_bundle_job_directory,
        SupportBundleJobManager::kProductionRetention,
        SupportBundleJobManager::kProductionRetryDelay,
        [] { return std::optional<std::string>("7K3M-9QFX-2DPA"); });
    assert(!context_validation.create(
               {false, SupportBundlePrivateRequest{invalid_context}}, error) &&
           error == "invalid_support_context");
    context_validation.shutdown();

    SupportBundleContext valid_context;
    valid_context.kind = SupportBundleContextKind::no_github;
    valid_context.problem_description = "schedule stopped";
    valid_context.contact = "radio@example.test";
    auto case_failure_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager case_failure(
        case_failure_executor, [] { return id('3'); }, root,
        remove_support_bundle_job_directory,
        SupportBundleJobManager::kProductionRetention,
        SupportBundleJobManager::kProductionRetryDelay,
        [] { return std::optional<std::string>{}; });
    assert(!case_failure.create(
               {false, SupportBundlePrivateRequest{valid_context}}, error) &&
           error == "case_id_generation_failed");
    case_failure.shutdown();

    auto shutdown_executor = std::make_shared<FakeExecutor>();
    SupportBundleJobManager shutdown_expiry(
        shutdown_executor, [] { return id('1'); }, root,
        remove_support_bundle_job_directory, std::chrono::hours(1), std::chrono::minutes(1));
    const auto shutdown_start = std::chrono::steady_clock::now();
    shutdown_expiry.shutdown();
    assert(std::chrono::steady_clock::now() - shutdown_start < std::chrono::seconds(1));
    shutdown_expiry.shutdown();

    assert(std::filesystem::exists(sibling / "keep"));
    std::filesystem::remove_all(root);
    std::cout << "support_bundle_job_manager_test: PASS\n";
}
