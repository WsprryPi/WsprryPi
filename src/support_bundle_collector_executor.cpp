#include "support_bundle_collector_executor.hpp"

#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
enum class WaitState {
    running,
    reaped,
    error,
};

WaitState wait_for_child(pid_t pid,
                         int options,
                         int &status,
                         int &wait_error) noexcept {
    for (;;) {
        const pid_t result = waitpid(pid, &status, options);
        if (result == pid) {
            return WaitState::reaped;
        }
        if (result == 0) {
            return WaitState::running;
        }
        if (errno != EINTR) {
            wait_error = errno;
            return WaitState::error;
        }
    }
}

bool child_is_group_leader(pid_t pid) noexcept {
    for (;;) {
        const pid_t process_group = getpgid(pid);
        if (process_group == pid) {
            return true;
        }
        if (process_group < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

void wait_for_termination(pid_t pid,
                          std::chrono::milliseconds grace,
                          int &status,
                          bool process_group_verified) noexcept {
    const auto signal_target = process_group_verified ? -pid : pid;

    (void)kill(signal_target, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + grace;
    while (std::chrono::steady_clock::now() < deadline) {
        int wait_error = 0;
        const WaitState wait_state =
            wait_for_child(pid, WNOHANG, status, wait_error);
        if (wait_state != WaitState::running) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    (void)kill(signal_target, SIGKILL);
    int wait_error = 0;
    (void)wait_for_child(pid, 0, status, wait_error);
}

bool stop_requested(std::mutex &mutex, bool &stop) noexcept {
    std::lock_guard lock(mutex);
    return stop;
}

bool write_private_file(const std::filesystem::path &path,
                        const std::string &contents) {
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                0600);
    if (descriptor < 0) return false;
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t count = write(descriptor, contents.data() + offset,
                                    contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(descriptor); return false; }
        offset += static_cast<std::size_t>(count);
    }
    return close(descriptor) == 0;
}

struct PrivateContextCleanup {
    std::filesystem::path directory;
    ~PrivateContextCleanup() {
        if (directory.empty()) return;
        std::error_code error;
        std::filesystem::remove(directory / "problem-description.txt", error);
        error.clear();
        std::filesystem::remove(directory / "contact.txt", error);
        error.clear();
        std::filesystem::remove(directory, error);
    }
};
}  // namespace

SupportBundleCollectorExecutor::SupportBundleCollectorExecutor(
    std::string executable,
    std::chrono::milliseconds timeout,
    std::chrono::milliseconds grace,
    std::string project_version)
    : executable_(std::move(executable)), timeout_(timeout), grace_(grace),
      project_version_(std::move(project_version)) {}

void SupportBundleCollectorExecutor::request_stop() noexcept {
    std::lock_guard lock(mutex_);
    stop_ = true;
}

SupportBundleExecutionResult SupportBundleCollectorExecutor::run(
    const SupportBundleExecutionContext &context) {
    struct stat executable_info {};
    if (executable_.empty() || executable_.front() != '/' ||
        lstat(executable_.c_str(), &executable_info) != 0 ||
        S_ISLNK(executable_info.st_mode) || !S_ISREG(executable_info.st_mode) ||
        access(executable_.c_str(), X_OK) != 0) {
        return {false, "collector_launch_failed", "Support collection failed."};
    }

    if (stop_requested(mutex_, stop_)) {
        return {false, "collector_cancelled", "Support collection stopped."};
    }

    std::vector<std::string> arguments = {executable_, "--output-dir",
                                          context.job_directory.string()};
    PrivateContextCleanup private_context_cleanup;
    if (context.probe_i2c) {
        arguments.emplace_back("--probe-i2c");
    }
    if ((context.case_id.empty() && context.support_context) ||
        (!context.case_id.empty() && !context.support_context)) {
        return {false, "collector_launch_failed", "Support collection failed."};
    }
    if (!context.case_id.empty()) {
        if (!context.support_context ||
            !valid_support_bundle_case_id(context.case_id) ||
            !valid_support_bundle_context(*context.support_context) ||
            project_version_.empty()) {
            return {false, "collector_launch_failed", "Support collection failed."};
        }
        arguments.insert(arguments.end(), {"--case-id", context.case_id,
                                           "--project-version", project_version_});
        const auto &support = *context.support_context;
        if (support.kind == SupportBundleContextKind::existing_github_issue) {
            arguments.insert(arguments.end(), {"--github-issue", support.issue_url});
        } else {
            const std::filesystem::path staging =
                context.job_directory / ".private-context";
            if (mkdir(staging.c_str(), 0700) != 0) {
                return {false, "collector_launch_failed", "Support collection failed."};
            }
            private_context_cleanup.directory = staging;
            const auto description = staging / "problem-description.txt";
            const auto contact = staging / "contact.txt";
            if (!write_private_file(description, support.problem_description) ||
                !write_private_file(contact, support.contact)) {
                return {false, "collector_launch_failed", "Support collection failed."};
            }
            arguments.insert(arguments.end(), {
                "--context-kind",
                support.kind == SupportBundleContextKind::new_github_issue
                    ? "new_github_issue" : "no_github",
                "--problem-description-file", description.string(),
                "--contact-file", contact.string(),
            });
        }
    }
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (auto &argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        return {false, "collector_launch_failed", "Support collection failed."};
    }
    if (pid == 0) {
        if (setpgid(0, 0) != 0) {
            _exit(127);
        }

        const int null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0 ||
            dup2(null_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (null_fd != STDOUT_FILENO && null_fd != STDERR_FILENO) {
            close(null_fd);
        }

        execv(executable_.c_str(), argv.data());
        _exit(127);
    }

    bool process_group_verified = false;
    if (setpgid(pid, pid) == 0) {
        process_group_verified = true;
    } else {
        const int setpgid_error = errno;
        if ((setpgid_error == EACCES || setpgid_error == ESRCH) &&
            child_is_group_leader(pid)) {
            process_group_verified = true;
        }
    }

    int status = 0;
    if (!process_group_verified) {
        // The process cannot safely be managed as an isolated group.  This is
        // a launch failure, so terminate only the direct child and reap it.
        wait_for_termination(pid, grace_, status, false);
        return {false, "collector_launch_failed", "Support collection failed."};
    }

    const auto started = std::chrono::steady_clock::now();
    for (;;) {
        int wait_error = 0;
        const WaitState wait_state =
            wait_for_child(pid, WNOHANG, status, wait_error);
        if (wait_state == WaitState::reaped) {
            break;
        }
        if (wait_state == WaitState::error) {
            if (wait_error != ECHILD) {
                wait_for_termination(pid, grace_, status, true);
            }
            return {false, "collector_launch_failed", "Support collection failed."};
        }

        const bool cancelled = stop_requested(mutex_, stop_);
        const bool timed_out = std::chrono::steady_clock::now() - started > timeout_;
        if (cancelled || timed_out) {
            wait_for_termination(pid, grace_, status, true);
            return cancelled
                       ? SupportBundleExecutionResult{false, "collector_cancelled",
                                                      "Support collection stopped."}
                       : SupportBundleExecutionResult{false, "collector_timeout",
                                                      "Support collection failed."};
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (stop_requested(mutex_, stop_)) {
        return {false, "collector_cancelled", "Support collection stopped."};
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return {true, {}, {}};
    }
    return {false, "collector_exit_failed", "Support collection failed."};
}
