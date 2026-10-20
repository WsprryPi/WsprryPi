#include "support_bundle_collector_executor.hpp"

#include <cassert>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
using namespace std::chrono_literals;

fs::path make_job_directory(const fs::path &root, const std::string &name) {
    const fs::path directory = root / name;
    assert(fs::create_directory(directory));
    return directory;
}

bool wait_for_file(const fs::path &path) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (fs::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

std::vector<std::string> read_lines(const fs::path &path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) {
        lines.push_back(line);
    }
    return lines;
}

pid_t helper_pid(const fs::path &job_directory) {
    std::ifstream input(job_directory / "executor-helper-pid.txt");
    pid_t pid = -1;
    input >> pid;
    return pid;
}

void require_reaped(pid_t pid) {
    assert(pid > 0);
    errno = 0;
    assert(kill(pid, 0) == -1);
    assert(errno == ESRCH);
}

void assert_sanitized_failure(const SupportBundleExecutionResult &result,
                              const fs::path &executable,
                              const fs::path &job_directory) {
    assert(!result.succeeded);
    assert(result.failure_message.find(executable.string()) == std::string::npos);
    assert(result.failure_message.find(job_directory.string()) == std::string::npos);
    assert(result.failure_message.find("raw-helper-output") == std::string::npos);
    assert(result.failure_message.find("/secret") == std::string::npos);
}

SupportBundleExecutionContext context_for(const fs::path &job_directory,
                                          bool probe_i2c = false) {
    return {probe_i2c, job_directory};
}

SupportBundleExecutionContext private_context_for(
    const fs::path &job_directory, SupportBundleContext support) {
    return {false, job_directory, "7K3M-9QFX-2DPA", std::move(support)};
}

SupportBundleCollectorExecutor private_executor(const fs::path &helper) {
    return SupportBundleCollectorExecutor(helper.string(), 1s, 50ms,
                                          "3.2.1-qualification.1");
}
}  // namespace

int main() {
    char template_path[] = "/tmp/wsprrypi-collector-executor-test.XXXXXX";
    assert(mkdtemp(template_path) != nullptr);
    const fs::path root(template_path);
    const fs::path helper =
        fs::absolute("build/bin/support_bundle_collector_executor_helper");

    {
        const fs::path job_directory = make_job_directory(root, "success");
        SupportBundleCollectorExecutor executor(helper.string(), 1s, 50ms);
        const auto result = executor.run(context_for(job_directory));
        assert(result.succeeded);

        const auto arguments = read_lines(job_directory / "executor-helper-argv.txt");
        assert(arguments.size() == 3);
        assert(arguments[0] == helper.string());
        assert(arguments[1] == "--output-dir");
        assert(arguments[2] == job_directory.string());
    }

    {
        const fs::path job_directory = make_job_directory(root, "directory with spaces");
        SupportBundleCollectorExecutor executor(helper.string(), 1s, 50ms);
        const auto result = executor.run(context_for(job_directory, true));
        assert(result.succeeded);

        const auto arguments = read_lines(job_directory / "executor-helper-argv.txt");
        assert(arguments.size() == 4);
        assert(arguments[1] == "--output-dir");
        assert(arguments[2] == job_directory.string());
        assert(arguments[3] == "--probe-i2c");
    }

    {
        const fs::path job_directory = make_job_directory(root, "existing issue");
        SupportBundleContext support;
        support.kind = SupportBundleContextKind::existing_github_issue;
        support.issue_url = "https://github.com/WsprryPi/WsprryPi/issues/414";
        auto executor = private_executor(helper);
        assert(executor.run(private_context_for(job_directory, support)).succeeded);
        const auto arguments = read_lines(job_directory / "executor-helper-argv.txt");
        assert(arguments.size() == 9);
        assert(arguments[3] == "--case-id" && arguments[4] == "7K3M-9QFX-2DPA");
        assert(arguments[5] == "--project-version" &&
               arguments[6] == "3.2.1-qualification.1");
        assert(arguments[7] == "--github-issue" && arguments[8] == support.issue_url);
    }

    {
        const fs::path job_directory = make_job_directory(root, "private files");
        SupportBundleContext support;
        support.kind = SupportBundleContextKind::no_github;
        support.problem_description = "transmitter stopped after schedule";
        support.contact = "radio@example.test";
        auto executor = private_executor(helper);
        assert(executor.run(private_context_for(job_directory, support)).succeeded);
        const auto arguments = read_lines(job_directory / "executor-helper-argv.txt");
        assert(arguments.size() == 13);
        const std::string joined = [&] {
            std::string value;
            for (const auto &argument : arguments) value += argument + "\n";
            return value;
        }();
        assert(joined.find(support.problem_description) == std::string::npos);
        assert(joined.find(support.contact) == std::string::npos);
        assert(joined.find("--problem-description-file") != std::string::npos);
        assert(joined.find("--contact-file") != std::string::npos);
        assert(joined.find("--project-version\n3.2.1-qualification.1\n") != std::string::npos);
        assert(!fs::exists(job_directory / ".private-context"));
    }

    {
        const fs::path job_directory = make_job_directory(root, "missing version");
        SupportBundleContext support;
        support.kind = SupportBundleContextKind::existing_github_issue;
        support.issue_url = "https://github.com/WsprryPi/WsprryPi/issues/414";
        SupportBundleCollectorExecutor executor(helper.string(), 1s, 50ms);
        const auto result = executor.run(private_context_for(job_directory, support));
        assert_sanitized_failure(result, helper, job_directory);
        assert(result.failure_category == "collector_launch_failed");
    }

    {
        const fs::path job_directory = make_job_directory(root, "nonzero");
        SupportBundleCollectorExecutor executor(helper.string(), 1s, 50ms);
        const auto result = executor.run(context_for(job_directory));
        assert_sanitized_failure(result, helper, job_directory);
        assert(result.failure_category == "collector_exit_failed");
    }

    {
        const fs::path job_directory = make_job_directory(root, "relative");
        SupportBundleCollectorExecutor executor(
            "support_bundle_collector_executor_helper", 1s, 50ms);
        const auto result = executor.run(context_for(job_directory));
        assert_sanitized_failure(result, helper, job_directory);
        assert(result.failure_category == "collector_launch_failed");
    }

    {
        const fs::path job_directory = make_job_directory(root, "missing");
        const fs::path missing = root / "does-not-exist";
        SupportBundleCollectorExecutor executor(missing.string(), 1s, 50ms);
        const auto result = executor.run(context_for(job_directory));
        assert_sanitized_failure(result, missing, job_directory);
        assert(result.failure_category == "collector_launch_failed");
    }

    {
        const fs::path job_directory = make_job_directory(root, "symlink");
        const fs::path link = root / "helper-link";
        assert(symlink(helper.c_str(), link.c_str()) == 0);
        SupportBundleCollectorExecutor executor(link.string(), 1s, 50ms);
        const auto result = executor.run(context_for(job_directory));
        assert_sanitized_failure(result, link, job_directory);
        assert(result.failure_category == "collector_launch_failed");
    }

    {
        const fs::path job_directory = make_job_directory(root, "prelaunch-stop");
        SupportBundleCollectorExecutor executor(helper.string(), 1s, 50ms);
        executor.request_stop();
        const auto result = executor.run(context_for(job_directory));
        assert_sanitized_failure(result, helper, job_directory);
        assert(result.failure_category == "collector_cancelled");
        assert(!fs::exists(job_directory / "executor-helper-started.txt"));
    }

    {
        const fs::path job_directory = make_job_directory(root, "timeout");
        SupportBundleCollectorExecutor executor(helper.string(), 50ms, 25ms);
        const auto result = executor.run(context_for(job_directory));
        assert_sanitized_failure(result, helper, job_directory);
        assert(result.failure_category == "collector_timeout");
        require_reaped(helper_pid(job_directory));
    }

    {
        const fs::path job_directory = make_job_directory(root, "blocked");
        SupportBundleCollectorExecutor executor(helper.string(), 1s, 25ms);
        auto future = std::async(std::launch::async, [&] {
            return executor.run(context_for(job_directory));
        });
        assert(wait_for_file(job_directory / "executor-helper-started.txt"));
        executor.request_stop();
        executor.request_stop();
        const auto result = future.get();
        assert_sanitized_failure(result, helper, job_directory);
        assert(result.failure_category == "collector_cancelled");
        require_reaped(helper_pid(job_directory));
    }

    fs::remove_all(root);
    std::cout << "support_bundle_collector_executor_test: PASS\n";
}
