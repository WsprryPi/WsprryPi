#include "privileged_network_admin.hpp"

#include "privileged_network_runtime.hpp"

#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

struct IniScan {
    std::optional<std::string> value;
    std::size_t security_sections = 0;
    std::size_t setting_lines = 0;
};

IniScan scan_ini(const std::string &contents) {
    IniScan scan;
    std::string section;
    std::size_t offset = 0;
    while (offset <= contents.size()) {
        const auto newline = contents.find('\n', offset);
        const auto length = newline == std::string::npos
            ? contents.size() - offset : newline - offset;
        const std::string line = contents.substr(offset, length);
        const std::string normalized = trim(line);
        if (normalized.size() >= 2 && normalized.front() == '[' &&
            normalized.back() == ']') {
            section = trim(std::string_view(normalized).substr(
                1, normalized.size() - 2));
            if (section == "Security") ++scan.security_sections;
        } else if (section == "Security" && !normalized.empty() &&
                   normalized.front() != ';' && normalized.front() != '#') {
            const auto equals = normalized.find('=');
            if (equals != std::string::npos &&
                trim(std::string_view(normalized).substr(0, equals)) ==
                    "Privileged Network Safety") {
                ++scan.setting_lines;
                scan.value = trim(std::string_view(normalized).substr(equals + 1));
            }
        }
        if (newline == std::string::npos) break;
        offset = newline + 1;
    }
    return scan;
}

std::string mode_value(PrivilegedNetworkMode mode) {
    return mode == PrivilegedNetworkMode::insecure_disabled
        ? "insecure-disabled" : "enforced";
}
}

std::optional<std::string> read_text_file(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::optional<std::string> read_privileged_network_ini_value(
    const std::string &contents) {
    const auto scan = scan_ini(contents);
    if (scan.security_sections > 1 || scan.setting_lines > 1)
        return std::nullopt;
    return scan.value;
}

std::optional<std::string> render_privileged_network_ini(
    const std::string &existing, const std::string &value) {
    if (!parse_privileged_network_mode(value).valid) return std::nullopt;
    const auto scan = scan_ini(existing);
    if (scan.security_sections > 1 || scan.setting_lines > 1)
        return std::nullopt;

    std::string output;
    std::string section;
    bool wrote_setting = false;
    bool saw_security = false;
    std::size_t offset = 0;
    while (offset < existing.size()) {
        const auto newline = existing.find('\n', offset);
        const auto end = newline == std::string::npos ? existing.size() : newline + 1;
        const std::string line = existing.substr(offset, end - offset);
        const std::string normalized = trim(line);
        if (normalized.size() >= 2 && normalized.front() == '[' &&
            normalized.back() == ']') {
            const std::string next_section = trim(std::string_view(normalized).substr(
                1, normalized.size() - 2));
            if (section == "Security" && !wrote_setting) {
                output += "Privileged Network Safety = " + value + "\n\n";
                wrote_setting = true;
            }
            section = next_section;
            saw_security = saw_security || section == "Security";
        }
        if (section == "Security" && !normalized.empty() &&
            normalized.front() != ';' && normalized.front() != '#') {
            const auto equals = normalized.find('=');
            if (equals != std::string::npos &&
                trim(std::string_view(normalized).substr(0, equals)) ==
                    "Privileged Network Safety") {
                output += "Privileged Network Safety = " + value + "\n";
                wrote_setting = true;
                offset = end;
                continue;
            }
        }
        output += line;
        offset = end;
    }
    if (saw_security && !wrote_setting) {
        if (!output.empty() && output.back() != '\n') output += '\n';
        output += "Privileged Network Safety = " + value + "\n";
    } else if (!saw_security) {
        if (!output.empty() && output.back() != '\n') output += '\n';
        if (!output.empty() && !output.ends_with("\n\n")) output += '\n';
        output += "[Security]\nPrivileged Network Safety = " + value + "\n";
    }
    return output;
}

bool write_text_file_atomically(const std::string &path,
                                const std::string &contents) {
    const std::filesystem::path destination(path);
    const auto parent = destination.parent_path();
    if (parent.empty()) return false;
    std::error_code error;
    if (!std::filesystem::is_directory(parent, error) || error) return false;

    std::string pattern = (parent / (destination.filename().string() + ".XXXXXX")).string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int descriptor = ::mkstemp(buffer.data());
    if (descriptor < 0) return false;
    const std::string temporary(buffer.data());
    mode_t mode = 0644;
    uid_t owner = ::getuid();
    gid_t group = ::getgid();
    bool preserve_owner = false;
    struct stat metadata{};
    if (::stat(path.c_str(), &metadata) == 0) {
        mode = metadata.st_mode & 0777;
        owner = metadata.st_uid;
        group = metadata.st_gid;
        preserve_owner = true;
    }
    bool ok = ::fchmod(descriptor, mode) == 0;
    if (ok && preserve_owner)
        ok = ::fchown(descriptor, owner, group) == 0;
    std::size_t written = 0;
    while (ok && written < contents.size()) {
        const auto count = ::write(descriptor, contents.data() + written,
                                   contents.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) ok = false;
        else written += static_cast<std::size_t>(count);
    }
    if (ok) ok = ::fsync(descriptor) == 0;
    if (::close(descriptor) != 0) ok = false;
    if (ok) ok = ::rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) (void)::unlink(temporary.c_str());
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        if (ok) ok = ::fsync(directory) == 0;
        (void)::close(directory);
    }
    return ok;
}

bool run_privileged_network_command(const std::vector<std::string> &arguments) {
    if (arguments.empty() || arguments.front().empty()) return false;
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments)
        argv.push_back(const_cast<char *>(argument.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child < 0) return false;
    if (child == 0) {
        ::execv(argv.front(), argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

PrivilegedNetworkAdmin::PrivilegedNetworkAdmin(
    PrivilegedNetworkAdminPaths paths, PrivilegedNetworkCommandRunner runner)
    : paths_(std::move(paths)), runner_(std::move(runner)) {}

PrivilegedNetworkTransactionSnapshot PrivilegedNetworkAdmin::snapshot() const {
    const auto runtime = privileged_network_runtime_state();
    const auto ini = read_text_file(paths_.ini_file);
    const auto policy = read_text_file(paths_.apache_policy_file);
    const auto scan = ini ? scan_ini(*ini) : IniScan{};
    const bool ambiguous = scan.security_sections > 1 || scan.setting_lines > 1;
    auto parsed = parse_privileged_network_mode(ambiguous ?
        std::optional<std::string>("<ambiguous>") : scan.value);
    return {parsed.mode, runtime.active, ini.has_value(), runtime.active_known,
            mode_value(parsed.mode), policy.value_or(""), ini.value_or(""),
            parsed.valid, parsed.missing};
}

PrivilegedNetworkTransactionSnapshot PrivilegedNetworkAdmin::status() {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot();
}

bool PrivilegedNetworkAdmin::validate_apache_candidate(
    const std::string &policy) const {
    const std::string candidate = paths_.apache_policy_file + ".candidate." +
        std::to_string(static_cast<long long>(::getpid()));
    if (!write_text_file_atomically(candidate, policy)) return false;
    const bool valid = runner_({paths_.apache_control, "-t", "-C",
                                "Include \"" + candidate + "\""});
    (void)::unlink(candidate.c_str());
    return valid;
}

PrivilegedNetworkTransactionResult PrivilegedNetworkAdmin::apply(
    const std::optional<std::string> &requested_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = snapshot();
    std::string expected_policy;
    const auto operations = PrivilegedNetworkTransactionOperations{
        [&](const PrivilegedNetworkTransactionCandidate &candidate) {
            if (!previous.configured_known) return false;
            const auto rendered = render_privileged_network_ini(
                previous.application_configuration, candidate.persisted_setting);
            return rendered.has_value() &&
                read_privileged_network_ini_value(*rendered) ==
                    std::optional<std::string>(candidate.persisted_setting);
        },
        [&](const std::string &policy) {
            expected_policy = policy;
            return validate_apache_candidate(policy);
        },
        [&](const PrivilegedNetworkTransactionCandidate &candidate) {
            const auto rendered = render_privileged_network_ini(
                previous.application_configuration, candidate.persisted_setting);
            return rendered.has_value() &&
                write_text_file_atomically(paths_.ini_file, *rendered) &&
                write_text_file_atomically(paths_.apache_policy_file,
                                           candidate.apache_policy);
        },
        [&] { return runner_({paths_.system_control, "reload", "apache2"}); },
        [&](PrivilegedNetworkMode mode) {
            const auto ini = read_text_file(paths_.ini_file);
            const auto policy = read_text_file(paths_.apache_policy_file);
            const auto parsed = ini
                ? parse_privileged_network_mode(
                    read_privileged_network_ini_value(*ini))
                : PrivilegedNetworkModeParseResult{};
            if (!ini || !policy || *policy != expected_policy ||
                !parsed.valid || parsed.mode != mode) {
                return false;
            }
            set_privileged_network_runtime_mode(mode);
            return true;
        },
        [&](const PrivilegedNetworkTransactionSnapshot &state) {
            return state.configured_known &&
                write_text_file_atomically(paths_.ini_file,
                                           state.application_configuration) &&
                write_text_file_atomically(paths_.apache_policy_file,
                                           state.apache_policy);
        }};
    PrivilegedNetworkTransaction transaction(previous, operations);
    auto result = transaction.apply(requested_value);
    if (result.status == PrivilegedNetworkTransactionStatus::rollback_failed)
        set_privileged_network_runtime_unknown();
    return result;
}
