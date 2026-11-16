// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "controller_runtime.hpp"

#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "logging.hpp"
#include "privileged_network_runtime.hpp"
#include "scheduling.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/fleet_runtime.hpp"
#include "wtp_integration/pico_management_journal.hpp"
#include "wtp_integration/pi_management_journal.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace {
std::atomic_bool controller_role{false}, controller_ready{false};
std::string controller_identity;

int port_number(const std::string &value) {
    int port{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        port < 1024 || port > 49151)
        throw std::invalid_argument("Controller listener ports must be integers from 1024 through 49151");
    return port;
}
}

bool controller_only_requested(int argc, char *argv[]) noexcept {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--controller-only") return true;
    return false;
}
bool controller_only_mode() noexcept { return controller_role.load(std::memory_order_acquire); }

void configure_controller_only(int argc, char *argv[]) {
    if (controller_only_mode()) throw std::logic_error("Controller role is already configured");
    std::string ini;
    std::optional<int> web_port, socket_port;
    bool role_seen = false;
    // Parse the complete invocation before loading files. Reject transmitter
    // flags and positional arguments rather than ignoring conflicting intent.
    for (int i = 1; i < argc; ++i) {
        const std::string option(argv[i]);
        auto argument = [&]() -> std::string {
            if (++i >= argc || std::string_view(argv[i]).starts_with('-'))
                throw std::invalid_argument("Missing controller option argument");
            return argv[i];
        };
        if (option == "--controller-only" && !role_seen) role_seen = true;
        else if ((option == "-i" || option == "--ini-file") && ini.empty()) ini = argument();
        else if (option.starts_with("--ini-file=") && ini.empty()) ini = option.substr(11);
        else if ((option == "-w" || option == "--web-port") && !web_port) web_port = port_number(argument());
        else if (option.starts_with("--web-port=") && !web_port) web_port = port_number(option.substr(11));
        else if ((option == "-k" || option == "--socket-port") && !socket_port) socket_port = port_number(argument());
        else if (option.starts_with("--socket-port=") && !socket_port) socket_port = port_number(option.substr(14));
        else if (option == "--socket-loopback-only") config.socket_loopback_only = true;
        else if (option == "--no-web") config.enable_web = false;
        else if (option == "--no-http") config.enable_http = false;
        else if (option == "--journald" || option == "-J") config.use_journald = true;
        else if (option == "--date-time-log" || option == "-D") config.date_time_log = true;
        else if (option == "--debug-logging") config.debug_logging = true;
        else if (option == "--no-debug-logging") config.debug_logging = false;
        else throw std::invalid_argument("Unsupported or repeated option in controller-only mode");
    }
    if (!role_seen || ini.empty())
        throw std::invalid_argument("--controller-only requires -i <file> for controller-owned state");
    // Read only the controller's existing listener and security settings. Never
    // deserialize, validate, normalize, or persist local station/output settings.
    const auto input_parent=std::filesystem::canonical(std::filesystem::path(ini).has_parent_path()?std::filesystem::path(ini).parent_path():".");
    iniFile.set_file_reader([ini,input_parent](const std::string &path){
        const auto parent=std::filesystem::path(path).has_parent_path()?std::filesystem::path(path).parent_path():std::filesystem::path(".");
        if(path!=ini||std::filesystem::canonical(parent)!=input_parent)
            throw std::runtime_error("controller_input_refused");
        return wsprrypi::PiManagementJournal::read_ini_input(path).bytes;
    });
    iniFile.set_file_writer([](const std::string &,const std::string &){throw std::runtime_error("controller_ini_read_only");});
    iniFile.set_filename(ini);
    config.ini_filename = ini;
    config.use_ini = true;
    config.transmit = false;
    config.loop_tx = false;
    config.web_port = web_port.value_or(31415);
    config.socket_port = socket_port.value_or(31416);
    const auto &data = iniFile.getData();
    const auto operation = data.find("Operation");
    if (operation != data.end()) {
        if (!web_port && operation->second.contains("Web Port"))
            config.web_port = port_number(iniFile.get_string_value("Operation", "Web Port"));
        if (!socket_port && operation->second.contains("Socket Port"))
            config.socket_port = port_number(iniFile.get_string_value("Operation", "Socket Port"));
    }
    if (config.enable_web && config.enable_http && config.web_port == config.socket_port)
        throw std::invalid_argument("Controller HTTP and WebSocket ports must differ");
    std::optional<std::string> security;
    const auto section = data.find("Security");
    if (section != data.end() && section->second.contains("Privileged Network Safety"))
        security = iniFile.get_string_value("Security", "Privileged Network Safety");
    initialize_privileged_network_runtime(security);
    controller_role.store(true, std::memory_order_release);
}

nlohmann::json controller_only_status() {
    return {{"role", "controller-only"}, {"controller_ready", controller_ready.load()},
        {"device_id", controller_identity}, {"local_output", nullptr},
        {"local_transmission_available", false}};
}

bool controller_http_allowed(std::string_view method, std::string_view path) {
    if (!controller_only_mode()) return true;
    if (method == "GET" && (path == "/" || path == "/version" || path == "/status" ||
        path == "/api/control/v1/status")) return true;
    if (!controller_ready.load(std::memory_order_acquire)) return false;
    if(path=="/api/control/v1/targets"||path.starts_with("/api/control/v1/targets/"))
        return method=="GET"||method=="PUT"||method=="POST"||method=="DELETE";
    if(path=="/api/control/v1/host/management-session")return method=="POST";
    if (method == "GET" && (path == "/api/control/v1/host/devices" ||
        path == "/api/control/v1/host/discovery" || path == "/api/control/v1/host/fleet" ||
        path == "/api/control/v1/host/fleet/operations" || path == "/api/control/v1/host/fleet/actions")) return true;
    return method == "POST" && (path == "/api/control/v1/host/devices" ||
        path == "/api/control/v1/host/discovery/identify" || path == "/api/control/v1/host/fleet");
}

bool controller_only_loop(const std::string &fixture_identity) {
    if (!controller_only_mode()) throw std::logic_error("Controller role was not selected");
    bool result = false;
    const char *stage = "identity";
    try {
        controller_identity = fixture_identity.empty() ? wsprrypi::wtp_pi_device_id() : fixture_identity;
        if (controller_identity.size() != 32 || controller_identity.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Controller identity is invalid");
        stage = "catalog";
        wsprrypi::WtpCatalog catalog(config.ini_filename + ".wtp-devices.json");
        (void)catalog.snapshot({});
        stage = "Pico management recovery";
        // Loading one journal validates the complete bounded directory and
        // unresolved-operation authority before any listener admits requests.
        wsprrypi::PicoManagementJournal recovery(config.ini_filename + ".management",
            controller_identity, controller_identity,catalog.management_history_authority({}));
        (void)recovery.snapshot();
        recovery.prune_retired();
        stage = "Fleet recovery";
        wsprrypi::start_wtp_fleet(config.ini_filename, controller_identity);
        const auto fleet = wsprrypi::wtp_fleet_api("GET", "", "");
        if (fleet.status != 200)
            throw std::runtime_error("Controller Fleet recovery state is unavailable");
        const auto state = nlohmann::json::parse(fleet.body);
        if (!state.is_object() || state.at("storage_error") != "" ||
            state.at("replacement_pending") != false || state.at("clock_fault") != false)
            throw std::runtime_error("Controller Fleet recovery state is invalid");
        if (config.enable_web && config.enable_http) {
            stage = "HTTP listener";
            webServer.start(config.web_port);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!webServer.isListening() && !exiting_wspr.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!webServer.isListening()) throw std::runtime_error("Controller HTTP listener unavailable");
        }
        stage = "WebSocket listener";
        if (config.enable_web && !socketServer.start(config.socket_port, SOCKET_KEEPALIVE,
                config.socket_loopback_only, config.socket_loopback_family))
            throw std::runtime_error("Controller WebSocket listener unavailable");
        controller_ready.store(true, std::memory_order_release);
        llog.logS(INFO, "Controller ready; no local transmitter output. Listener settings require process restart.");
        std::unique_lock lock(exitwspr_mtx);
        exitwspr_cv.wait(lock, [] { return exitwspr_ready; });
        result = true;
    } catch (const std::exception &) {
        // Do not relay private profile contents or filesystem paths to browsers.
        llog.logS(ERROR, "Controller startup failed at ", stage, "; inspect controller identity, catalog, recovery state, and listeners.");
    }
    controller_ready.store(false, std::memory_order_release);
    webServer.stop();
    socketServer.stop();
    wsprrypi::stop_wtp_fleet();
    return result;
}
