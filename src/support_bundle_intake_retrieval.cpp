#include "support_bundle_intake_retrieval.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iomanip>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <exception>
#include <stdexcept>
#include <mutex>
#include <set>

namespace {

enum class FetchStatus { success, launch_failed, timed_out, failed, empty, oversized };

struct FetchResult {
    FetchStatus status = FetchStatus::failed;
    std::string bytes;
    std::string cleanup_diagnostic = {};
};

bool valid_executable(const std::filesystem::path &path, bool test_seam) {
    if (!path.is_absolute()) return false;
    struct stat information{};
    if (!test_seam && path != "/usr/bin/curl") return false;
    return lstat(path.c_str(), &information) == 0 && !S_ISLNK(information.st_mode) &&
           S_ISREG(information.st_mode) &&
           (test_seam ? (information.st_uid == 0 || information.st_uid == geteuid())
                      : information.st_uid == 0) &&
           (information.st_mode & 0022) == 0 && access(path.c_str(), X_OK) == 0;
}

std::string curl_seconds(std::chrono::milliseconds value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << static_cast<double>(value.count()) / 1000.0;
    return output.str();
}

std::mutex pending_children_mutex;
std::set<pid_t> pending_children;

std::string pending_cleanup_locked(bool force_pending) {
    for (auto child=pending_children.begin();child!=pending_children.end();) {
        const auto observed = force_pending ? 0 : waitpid(*child,nullptr,WNOHANG);
        if (observed==*child || (observed<0 && errno==ECHILD)) child=pending_children.erase(child);
        else ++child;
    }
    if (pending_children.empty()) return {};
    return "stage=intake-child-cleanup; expected=previous owned child reaped before another launch; child="+
        std::to_string(*pending_children.begin())+"; cleanup incomplete";
}

std::string terminate_and_reap(pid_t child, bool force_pending = false) {
    if (child <= 0) return {};
    kill(-child, SIGTERM);
    kill(child, SIGTERM);
    bool leader_reaped = false;
    const auto observe = [&] {
        if (!leader_reaped && !force_pending) {
            const auto waited = waitpid(child,nullptr,WNOHANG);
            leader_reaped = waited==child || (waited<0 && errno==ECHILD);
        }
    };
    // Preserve the existing 100ms TERM grace; it is escalation policy, not readiness.
    const auto grace = std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
    do { observe(); (void)poll(nullptr,0,5); } while (std::chrono::steady_clock::now()<grace);
    kill(-child,SIGKILL);
    if (!leader_reaped) kill(child,SIGKILL);
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!leader_reaped && std::chrono::steady_clock::now()<deadline) {
        observe();
        if (!leader_reaped) (void)poll(nullptr,0,1);
    }
    if (leader_reaped) return {};
    { std::lock_guard lock(pending_children_mutex); pending_children.insert(child); }
    return "stage=intake-child-cleanup; expected=owned child reaped; last_state=SIGKILL sent; child="+
        std::to_string(child)+"; hang_guard=2s; cleanup incomplete";
}

FetchResult fetch(const SupportBundleIntakeRetrievalRequest &request,
                  const std::string &url,
                  std::size_t maximum_bytes,
                  const SupportBundleIntakeRetrievalTestHooks &hooks) {
    int output_pipe[2] = {-1, -1};
    if (pipe(output_pipe) != 0) return {FetchStatus::launch_failed, {}};
    if (fcntl(output_pipe[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(output_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return {FetchStatus::launch_failed, {}};
    }

    const auto connect_timeout = curl_seconds(request.connect_timeout);
    const auto operation_timeout = curl_seconds(request.operation_timeout);
    const auto maximum = std::to_string(maximum_bytes);
    std::vector<std::string> arguments = {
        request.curl_executable.string(), "--disable", "--silent", "--show-error", "--fail",
        "--ipv4",
        "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "0",
        "--connect-timeout", connect_timeout, "--max-time", operation_timeout,
        "--max-filesize", maximum, "--http1.1", "--request", "GET", "--output", "-",
        "--write-out", "\n%{http_code}", "--url", url};
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (auto &argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::array<char *, 5> environment = {
        const_cast<char *>("LANG=C"), const_cast<char *>("LC_ALL=C"),
        const_cast<char *>("HOME=/nonexistent"), const_cast<char *>("PATH=/usr/bin:/bin"), nullptr};

    // Linearize admission with retained-child registration; never hold this
    // mutex during child readiness, operation or cleanup waits.
    std::unique_lock admission(pending_children_mutex);
    const auto pending = pending_cleanup_locked(hooks.force_reap_pending);
    if (!pending.empty()) {
        close(output_pipe[0]); close(output_pipe[1]);
        return {FetchStatus::launch_failed,{},pending};
    }
    const pid_t child = fork();
    if (child != 0) admission.unlock();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return {FetchStatus::launch_failed, {}};
    }
    if (child == 0) {
        if (setpgid(0, 0) != 0) _exit(126);
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0) _exit(126);
        if (output_pipe[1] != STDOUT_FILENO) close(output_pipe[1]);
        const int null_descriptor = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_descriptor < 0 || dup2(null_descriptor, STDERR_FILENO) < 0) _exit(126);
        if (null_descriptor != STDERR_FILENO) close(null_descriptor);
        execve(request.curl_executable.c_str(), argv.data(), environment.data());
        _exit(127);
    }
    bool group_ready = false;
    if (!hooks.fail_parent_process_group_setup) {
        if (setpgid(child, child) == 0) {
            group_ready = true;
        } else if (errno == EACCES || errno == EPERM) {
            group_ready = getpgid(child) == child;
        } else if (errno == ESRCH) {
            // The child cannot reach exec unless its own setpgid succeeded.
            group_ready = true;
        }
    }
    close(output_pipe[1]);
    if (!group_ready) {
        close(output_pipe[0]);
        const auto cleanup = terminate_and_reap(child,hooks.force_reap_pending);
        return {FetchStatus::launch_failed, {},cleanup};
    }
    const int flags = fcntl(output_pipe[0], F_GETFL, 0);
    if (flags < 0 || fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        close(output_pipe[0]);
        const auto cleanup = terminate_and_reap(child,hooks.force_reap_pending);
        return {FetchStatus::launch_failed, {},cleanup};
    }
    try {
        if (hooks.after_spawn) hooks.after_spawn(child, url);
    } catch (...) {
        close(output_pipe[0]);
        const auto primary = std::current_exception();
        const auto cleanup = terminate_and_reap(child,hooks.force_reap_pending);
        if (cleanup.empty()) std::rethrow_exception(primary);
        try { std::rethrow_exception(primary); }
        catch (const std::exception& error) {
            std::throw_with_nested(std::runtime_error(std::string("primary=")+error.what()+"; "+cleanup));
        }
        catch (...) { std::throw_with_nested(std::runtime_error("primary=fixture gate failure; "+cleanup)); }
    }
    const auto deadline = std::chrono::steady_clock::now() + request.operation_timeout;
    std::string bytes;
    bytes.reserve(std::min<std::size_t>(maximum_bytes + 4, 4096));
    bool eof = false;
    bool oversized = false;
    bool timed_out = false;
    int child_status = 0;
    bool reaped = false;
    bool status_known = false;
    while (!eof || !reaped) {
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            break;
        }
        pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
        poll(&descriptor, 1, 10);
        if (descriptor.revents & (POLLIN | POLLHUP)) {
            std::array<char, 4096> buffer{};
            while (true) {
                const auto count = read(output_pipe[0], buffer.data(), buffer.size());
                if (count > 0) {
                    if (bytes.size() + static_cast<std::size_t>(count) > maximum_bytes + 4) {
                        oversized = true;
                        break;
                    }
                    bytes.append(buffer.data(), static_cast<std::size_t>(count));
                    continue;
                }
                if (count == 0) eof = true;
                if (count < 0 && errno == EINTR) continue;
                break;
            }
        }
        if (oversized) break;
        const auto waited = waitpid(child, &child_status, WNOHANG);
        if (waited == child) {
            reaped = true;
            status_known = true;
        }
        else if (waited < 0 && errno == ECHILD) reaped = true;
    }
    close(output_pipe[0]);
    const auto cleanup = timed_out || oversized || !reaped
        ? terminate_and_reap(child,hooks.force_reap_pending) : std::string{};
    if (timed_out) return {FetchStatus::timed_out, {},cleanup};
    if (oversized) return {FetchStatus::oversized, {},cleanup};
    if (!cleanup.empty()) return {FetchStatus::failed, {},cleanup};
    if (!status_known) return {FetchStatus::failed, {}};
    if (WIFEXITED(child_status) &&
        (WEXITSTATUS(child_status) == 126 || WEXITSTATUS(child_status) == 127))
        return {FetchStatus::launch_failed, {}};
    if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        return {FetchStatus::failed, {}};
    if (bytes.size() < 4 || bytes[bytes.size() - 4] != '\n' ||
        bytes.compare(bytes.size() - 3, 3, "200") != 0)
        return {FetchStatus::failed, {}};
    bytes.resize(bytes.size() - 4);
    if (bytes.empty()) return {FetchStatus::empty, {}};
    return {FetchStatus::success, std::move(bytes)};
}

SupportBundleIntakeRetrievalFailure map_failure(FetchStatus status, bool manifest) {
    if (manifest) {
        if (status == FetchStatus::launch_failed) return SupportBundleIntakeRetrievalFailure::launch_failed;
        if (status == FetchStatus::timed_out) return SupportBundleIntakeRetrievalFailure::manifest_timeout;
        if (status == FetchStatus::empty) return SupportBundleIntakeRetrievalFailure::manifest_empty;
        if (status == FetchStatus::oversized) return SupportBundleIntakeRetrievalFailure::manifest_oversized;
        return SupportBundleIntakeRetrievalFailure::manifest_failed;
    }
    if (status == FetchStatus::launch_failed) return SupportBundleIntakeRetrievalFailure::launch_failed;
    if (status == FetchStatus::timed_out) return SupportBundleIntakeRetrievalFailure::signature_timeout;
    if (status == FetchStatus::empty) return SupportBundleIntakeRetrievalFailure::signature_empty;
    if (status == FetchStatus::oversized) return SupportBundleIntakeRetrievalFailure::signature_oversized;
    return SupportBundleIntakeRetrievalFailure::signature_failed;
}

} // namespace

SupportBundleIntakeRetrievalResult retrieve_internal(
    const SupportBundleIntakeRetrievalRequest &request,
    bool test_seam,
    const SupportBundleIntakeRetrievalTestHooks &hooks) {
    if (request.manifest_url != kWsprryPiIntakeManifestUrl ||
        request.signature_url != kWsprryPiIntakeSignatureUrl ||
        request.connect_timeout.count() <= 0 || request.operation_timeout.count() <= 0 ||
        request.connect_timeout > request.operation_timeout ||
        request.connect_timeout > std::chrono::seconds(30) ||
        request.operation_timeout > std::chrono::seconds(60) ||
        request.maximum_manifest_bytes != 16 * 1024 ||
        request.maximum_signature_bytes != 2 * 1024)
        return {SupportBundleIntakeRetrievalFailure::invalid_request, {}, {}};
    if (!valid_executable(request.curl_executable, test_seam))
        return {SupportBundleIntakeRetrievalFailure::executable_unavailable, {}, {}};
    const auto manifest = fetch(request, request.manifest_url, request.maximum_manifest_bytes, hooks);
    if (manifest.status != FetchStatus::success)
        return {map_failure(manifest.status, true), {}, {},manifest.cleanup_diagnostic};
    const auto signature = fetch(request, request.signature_url, request.maximum_signature_bytes, hooks);
    if (signature.status != FetchStatus::success)
        return {map_failure(signature.status, false), {}, {},signature.cleanup_diagnostic};
    return {SupportBundleIntakeRetrievalFailure::none, manifest.bytes, signature.bytes};
}

SupportBundleIntakeRetrievalResult retrieve_support_bundle_intake(
    const SupportBundleIntakeRetrievalRequest &request) {
    return retrieve_internal(request, false, {});
}

SupportBundleIntakeRetrievalResult retrieve_support_bundle_intake_for_test(
    const SupportBundleIntakeRetrievalRequest &request,
    const SupportBundleIntakeRetrievalTestHooks &hooks) {
    return retrieve_internal(request, true, hooks);
}
