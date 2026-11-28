// SPDX-License-Identifier: MIT
#include "controller_profile_entry.hpp"
#include "backend_capabilities.hpp"
#include "version.hpp"
#include <iostream>
#include <string_view>

std::optional<int> controller_profile_entry(int argc, char *argv[]) {
#if WSPRRYPI_CONTROLLER_ONLY
    bool controller = false;
    int commands = 0;
    bool maintenance_worker = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--controller-only") controller = true;
        if (option == "--web-service" || option == "--settings" || option == "--maintenance" ||
            option == "--diagnostic-export" || option == "--recovery-backend" ||
            option == "--export-recovery-age-helper" || option == "--export-udp-logging-worker" ||
            option == "--storage-format") ++commands;
        if (option == "--maintenance-worker") { maintenance_worker = true; ++commands; }
    }
    const auto refused = [] {
        std::cerr << "This controller build requires --controller-only and has no local transmitter.\n";
        return std::optional<int>{2};
    };
    if (commands > 1) return refused();
    if (maintenance_worker) {
        if (argc != 4 || std::string_view(argv[1]) != "--maintenance-worker" ||
            std::string_view(argv[2]) != "--role" || std::string_view(argv[3]) != "controller") return refused();
    }
    if (commands) return {};
    if (argc == 2 || (argc == 3 && controller)) {
        const std::string_view option(argc == 3 && std::string_view(argv[1]) == "--controller-only" ? argv[2] : argv[1]);
        if (option == "--help" || option == "-h") {
            std::cout << get_version_string() << "\nBuild profile: controller\n\nUsage:\n"
                "  wsprrypi --controller-only --state-directory /absolute/private/state [options]\n"
                "  wsprrypi --initialize-state --controller-only --state-directory /absolute/private/state\n"
                "  wsprrypi --migrate-state --controller-only --state-directory /absolute/private/state --ini-source /absolute/source.ini\n\n"
                "No station fields or local transmission backends are available.\n"
                "Options: --runtime-directory, --web-port, --socket-port, --socket-loopback-only,\n"
                "         --no-web, --no-http, --debug-logging, --no-debug-logging, --journald, --date-time-log.\n"
                "Local commands: --settings --help, --maintenance help, --diagnostic-export.\n";
            return 0;
        }
        if (option == "--version" || option == "-v") {
            std::cout << get_version_string() << "\nBuild profile: controller\nCompiled backends: none\nAncillary GPIO: disabled\n";
            return 0;
        }
        if (option == "--artifact-identity") {
            std::cout << controller_build_identity() << '\n';
            return 0;
        }
        if (option == "--list-backends") { std::cout << '\n'; return 0; }
    }
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help" || option == "-h" || option == "--version" || option == "-v" ||
            option == "--artifact-identity" || option == "--list-backends") return refused();
    }
    if (!controller) return refused();
#else
    (void)argc;
    (void)argv;
#endif
    return {};
}
