/**
 * @file web_server_version_routes.cpp
 * @brief Registers build and platform version metadata endpoints.
 */

#include "web_server_routes.hpp"

#include "config_handler.hpp"
#include "httplib.hpp"
#include "json.hpp"
#include "version.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace web_server_routes
{
namespace
{
std::vector<std::string> split_version_identifiers(const std::string &value)
{
    std::vector<std::string> identifiers;
    std::stringstream stream(value);
    std::string item;

    while (std::getline(stream, item, '.')) {
        if (item.empty()) {
            identifiers.clear();
            return identifiers;
        }
        identifiers.push_back(item);
    }

    return identifiers;
}

bool is_numeric_identifier(const std::string &value)
{
    return !value.empty()
           && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
              });
}

nlohmann::json parse_version_for_update_metadata(const std::string &version)
{
    nlohmann::json parsed = {
        {"valid", false},
        {"raw", version},
        {"major", nullptr},
        {"minor", nullptr},
        {"patch", nullptr},
        {"prerelease", nlohmann::json::array()},
        {"build", nlohmann::json::array()}
    };
    std::string source = version;
    if (!source.empty() && source.front() == 'v') {
        source.erase(source.begin());
    }

    const std::size_t build_pos = source.find('+');
    const std::string build = build_pos == std::string::npos ? "" : source.substr(build_pos + 1);
    source = build_pos == std::string::npos ? source : source.substr(0, build_pos);

    const std::size_t prerelease_pos = source.find('-');
    const std::string prerelease = prerelease_pos == std::string::npos ? "" : source.substr(prerelease_pos + 1);
    source = prerelease_pos == std::string::npos ? source : source.substr(0, prerelease_pos);

    const std::vector<std::string> core = split_version_identifiers(source);
    if (
        core.size() != 3 ||
        !is_numeric_identifier(core[0]) ||
        !is_numeric_identifier(core[1]) ||
        !is_numeric_identifier(core[2])
    ) {
        return parsed;
    }

    parsed["valid"] = true;
    parsed["major"] = std::stoul(core[0]);
    parsed["minor"] = std::stoul(core[1]);
    parsed["patch"] = std::stoul(core[2]);
    parsed["prerelease"] = prerelease.empty()
        ? nlohmann::json::array()
        : nlohmann::json(split_version_identifiers(prerelease));
    parsed["build"] = build.empty()
        ? nlohmann::json::array()
        : nlohmann::json(split_version_identifiers(build));
    return parsed;
}

nlohmann::json build_dirty_metadata(const std::string &dirty)
{
    if (dirty == "true") {
        return {
            {"known", true},
            {"dirty", true},
            {"raw", dirty}
        };
    }
    if (dirty == "false") {
        return {
            {"known", true},
            {"dirty", false},
            {"raw", dirty}
        };
    }

    return {
        {"known", false},
        {"dirty", nullptr},
        {"raw", dirty}
    };
}

} // namespace

void register_version(
    httplib::Server &server,
    CorsHeaderSetter set_cors_headers)
{
    server.Get(
        "/version",
        [set_cors_headers](
            const httplib::Request &, httplib::Response &response) {
            set_cors_headers(response);
            const std::string version =
                get_raw_version_string()
                + " on a "
                + get_pi_model()
                + ", "
                + get_os_version_name()
                + (sizeof(void *) == 8 ? " 64-bit" : " 32-bit")
                + " OS.";

            nlohmann::json j;
            j["wspr_version"] = version;
            j["ui_version"] = get_raw_version_string();
            j["wspr_version_raw"] = get_exe_version();
            j["wspr_version_parsed"] = parse_version_for_update_metadata(
                get_exe_version());
            j["wspr_branch"] = get_exe_raw_branch();
            j["wspr_branch_state"] = get_exe_branch_state();
            j["wspr_display_branch"] = get_exe_branch();
            j["wspr_exe_version"] = get_exe_version();
            j["wspr_commit"] = get_exe_commit();
            j["wspr_build_dirty"] = get_exe_build_dirty();
            j["wspr_build_dirty_state"] = build_dirty_metadata(
                get_exe_build_dirty());
            j["compiled_backends"] = get_compiled_backends();
            j["ancillary_gpio"] = has_ancillary_gpio();
            response.set_content(j.dump(4), "application/json");
        });
}
} // namespace web_server_routes
