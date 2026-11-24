#include "privileged_network_admin.hpp"

#include "privileged_network_runtime.hpp"

#include <cerrno>
#include <cctype>
#include <filesystem>
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <fstream>
#include <iterator>
#include <string_view>
#include <stdexcept>
#include <utility>

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
#ifdef _WIN32
    try{const auto bytes=wsprrypi::windows_file::read_protected(path,false,1048576);return std::string(bytes.begin(),bytes.end());}
    catch(const std::exception &){return std::nullopt;}
#else
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
#endif
}

std::optional<std::string> read_privileged_network_ini_value(
    const std::string &contents) {
    const auto scan = scan_ini(contents);
    if (scan.security_sections > 1 || scan.setting_lines > 1)
        return std::nullopt;
    return scan.value;
}

PrivilegedNetworkModeParseResult parse_privileged_network_ini_mode(const std::string &contents) {
    const auto scan=scan_ini(contents);
    return parse_privileged_network_mode(scan.security_sections>1||scan.setting_lines>1?
        std::optional<std::string>("<ambiguous>"):scan.value);
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
#ifdef _WIN32
    (void)path;(void)contents;
    return false;
#else
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
#endif
}

bool run_privileged_network_command(const std::vector<std::string> &arguments) {
#ifdef _WIN32
    (void)arguments;
    return false;
#else
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
#endif
}

PrivilegedNetworkAdmin::PrivilegedNetworkAdmin(
    PrivilegedNetworkAdminPaths paths, PrivilegedNetworkCommandRunner runner,PrivilegedNetworkIniIO ini_io,
    PrivilegedNetworkSettingsIO settings_io)
    : paths_(std::move(paths)), runner_(std::move(runner)),ini_io_(std::move(ini_io)),settings_io_(std::move(settings_io)) {
    if(bool(ini_io_.read)!=bool(ini_io_.write))throw std::invalid_argument("Install both INI read and write owners");
    if(bool(settings_io_.read)!=bool(settings_io_.write)|| (settings_io_.read&&ini_io_.read))
        throw std::invalid_argument("Install one complete network settings owner");
}

std::optional<std::string> PrivilegedNetworkAdmin::read_application_ini() const {
    return ini_io_.read?ini_io_.read(paths_.ini_file):read_text_file(paths_.ini_file);
}
bool PrivilegedNetworkAdmin::write_application_ini(const std::string &contents){
    try{
        const auto current=read_application_ini();if(!current)return false;
        if(ini_io_.write)return ini_io_.write(paths_.ini_file,*current,contents);
        return write_text_file_atomically(paths_.ini_file,contents);
    }catch(...){return false;}
}

PrivilegedNetworkTransactionSnapshot PrivilegedNetworkAdmin::snapshot() const {
    const auto runtime = privileged_network_runtime_state();
    if(settings_io_.read) {
        const auto saved=settings_io_.read();
        const auto policy=read_text_file(paths_.apache_policy_file);
        const auto mode=saved?saved->mode:PrivilegedNetworkMode::enforced;
        return {mode,runtime.active,saved.has_value(),runtime.active_known,
            mode_value(mode),policy.value_or(""),saved?saved->revision:std::string{},saved.has_value(),false};
    }
    const auto ini = read_application_ini();
    const auto policy = read_text_file(paths_.apache_policy_file);
    const auto parsed=ini?parse_privileged_network_ini_mode(*ini):parse_privileged_network_mode(std::nullopt);
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
#ifdef _WIN32
    (void)policy;
    return false;
#else
    const std::string candidate = paths_.apache_policy_file + ".candidate." +
        std::to_string(static_cast<long long>(::getpid()));
    if (!write_text_file_atomically(candidate, policy)) return false;
    const bool valid = runner_({paths_.apache_control, "-t", "-C",
                                "Include \"" + candidate + "\""});
    (void)::unlink(candidate.c_str());
    return valid;
#endif
}

PrivilegedNetworkTransactionResult PrivilegedNetworkAdmin::apply(
    const std::optional<std::string> &requested_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous = snapshot();
    std::string expected_policy;
    auto expected_revision=previous.application_configuration;
    const auto write_setting=[&](PrivilegedNetworkMode mode) {
        try {
            const auto before=settings_io_.read();
            if(!before||before->revision!=expected_revision)return false;
            const auto committed=settings_io_.write(expected_revision,mode);
            if(!committed)return false;
            expected_revision=*committed;
            const auto after=settings_io_.read();
            if(!after||after->mode!=mode||after->revision!=expected_revision||
                (mode!=before->mode&&after->revision==before->revision))return false;
            return true;
        }catch(...){return false;}
    };
    const auto operations = PrivilegedNetworkTransactionOperations{
        [&](const PrivilegedNetworkTransactionCandidate &candidate) {
            if (!previous.configured_known) return false;
            if(settings_io_.read)return !expected_revision.empty();
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
            if(settings_io_.read)
                return write_setting(candidate.requested)&&
                    write_text_file_atomically(paths_.apache_policy_file,candidate.apache_policy);
            const auto rendered = render_privileged_network_ini(
                previous.application_configuration, candidate.persisted_setting);
            return rendered.has_value() &&
                write_application_ini(*rendered) &&
                write_text_file_atomically(paths_.apache_policy_file,
                                           candidate.apache_policy);
        },
        [&] { return runner_({paths_.system_control, "reload", "apache2"}); },
        [&](PrivilegedNetworkMode mode) {
            if(settings_io_.read) {
                const auto saved=settings_io_.read();
                const auto policy=read_text_file(paths_.apache_policy_file);
                if(!saved||saved->mode!=mode||saved->revision!=expected_revision||!policy||*policy!=expected_policy)return false;
                set_privileged_network_runtime_mode(mode);
                return true;
            }
            const auto ini = read_application_ini();
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
            if(settings_io_.read)
                return state.configured_known&&write_setting(state.configured)&&
                    write_text_file_atomically(paths_.apache_policy_file,state.apache_policy);
            return state.configured_known &&
                write_application_ini(state.application_configuration) &&
                write_text_file_atomically(paths_.apache_policy_file,
                                           state.apache_policy);
        }};
    PrivilegedNetworkTransaction transaction(previous, operations);
    auto result = transaction.apply(requested_value);
    if (result.status == PrivilegedNetworkTransactionStatus::rollback_failed)
        set_privileged_network_runtime_unknown();
    return result;
}
