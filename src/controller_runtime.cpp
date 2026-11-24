#include "application_control/backend_services.hpp"
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Lee Bussy
#include "controller_runtime.hpp"
#include "application_storage/controller_identity.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/controller_settings.hpp"
#include "application_storage/sqlite_catalog.hpp"
#include "application_control/local_transport.hpp"

#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "logging.hpp"
#include "privileged_network_runtime.hpp"
#include "scheduling.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"
#include "wtp_endpoint/identity.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/pi_pair_local_routes.hpp"
#include "wtp_integration/pi_pair_owner.hpp"
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
std::atomic_bool controller_role{false}, controller_ready{false},identity_fenced{false};
bool verify_controller_identity{};
std::string controller_identity,controller_boot;
std::unique_ptr<wsprrypi::ControllerSettings> controller_settings;
std::unique_ptr<wsprrypi::WtpCatalog> controller_catalog;

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
bool controller_only_ready() noexcept {
    if(!controller_ready.load(std::memory_order_acquire))return false;
    if(verify_controller_identity&&(identity_fenced.load()||!wsprrypi::controller_identity_matches(controller_identity))){
        identity_fenced.store(true);return false;
    }
    return true;
}

void configure_controller_only(int argc, char *argv[]) {
    if (controller_only_mode()) throw std::logic_error("Controller role is already configured");
    std::string ini;
    std::optional<int> web_port, socket_port;
    std::optional<bool> debug_override;
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
        else if (option == "--debug-logging") {config.debug_logging = true;debug_override=true;}
        else if (option == "--no-debug-logging") {config.debug_logging = false;debug_override=false;}
        else throw std::invalid_argument("Unsupported or repeated option in controller-only mode");
    }
    if(wsprrypi::application_database_selected()) {
        const auto database=wsprrypi::application_database();
        if(!role_seen||!ini.empty()||database->role()!=wsprrypi::StateRole::Controller)
            throw std::invalid_argument("Controller settings require the explicitly selected controller state");
        auto settings=std::make_unique<wsprrypi::ControllerSettings>(database);
        const auto saved=settings->snapshot().settings;
        config.ini_filename=wsprrypi::application_configuration_namespace();config.use_ini=true;
        config.transmit=false;config.loop_tx=false;
        config.web_port=web_port.value_or(saved.configuration.at("Operation").at("Web Port").get<int>());
        config.socket_port=static_cast<std::uint16_t>(socket_port.value_or(saved.configuration.at("Operation").at("Socket Port").get<int>()));
        config.debug_logging=debug_override.value_or(saved.private_settings.debug_logging);
        if(config.enable_web&&config.enable_http&&config.web_port==config.socket_port)
            throw std::invalid_argument("Controller HTTP and WebSocket ports must differ");
        auto catalog=std::make_unique<wsprrypi::WtpCatalog>(wsprrypi::sqlite_catalog_storage(database));
        (void)catalog->snapshot({});
        iniFile.set_file_reader([](const std::string &)->std::string{throw std::runtime_error("Controller database does not read INI files");});
        iniFile.set_file_writer([](const std::string &,const std::string &){throw std::runtime_error("Controller database does not write INI files");});
        initialize_privileged_network_runtime(saved.private_settings.network_safety==wsprrypi::SavedNetworkSafety::Enforced?"enforced":"insecure-disabled");
        controller_settings=std::move(settings);controller_catalog=std::move(catalog);
        controller_role.store(true,std::memory_order_release);return;
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

std::string controller_only_process_boot(){return controller_only_ready()?controller_boot:std::string{};}
std::string prepare_controller_process_boot(){
    if(!controller_only_mode())throw std::logic_error("Controller role has not been selected");
    if(controller_boot.empty())controller_boot=wsprrypi::WtpPiBootIdentity{}.new_boot_id();
    return controller_boot;
}
std::string persist_controller_advanced_settings(const std::string &expected,const nlohmann::json &changes){
    std::lock_guard lock(config_update_mutex());
    if(!controller_only_mode()||!controller_settings)throw wsprrypi::LocalCommandRefused(wsprrypi::LocalRefusal::StorageUnavailable);
    if(!controller_only_ready()||exiting_wspr.load())
        throw wsprrypi::LocalCommandRefused(wsprrypi::LocalRefusal::Busy);
    const auto revision=controller_settings->persist_advanced(expected,changes);
    if(changes.contains("Meta.debug_logging")){
        config.debug_logging=changes.at("Meta.debug_logging").get<bool>();refresh_logger_level_from_config();
    }
    // Listener processes retain their actual bindings until explicit restart.
    return revision;
}
wsprrypi::WtpCatalog &controller_storage_catalog(){
    if(!controller_only_mode()||!wsprrypi::application_database_selected()||!controller_catalog)
        throw std::runtime_error("controller_storage_unavailable");
    return *controller_catalog;
}
std::string persist_controller_network_safety_setting(const std::string &revision,bool insecure_disabled){
    if(!controller_only_mode()||!wsprrypi::application_database_selected()||!controller_settings)
        throw std::runtime_error("controller_storage_unavailable");
    if(!controller_identity.empty()&&!controller_only_ready())throw wsprrypi::LocalCommandRefused(wsprrypi::LocalRefusal::StorageUnavailable);
    return controller_settings->persist_network_safety(revision,insecure_disabled?
        wsprrypi::SavedNetworkSafety::InsecureDisabled:wsprrypi::SavedNetworkSafety::Enforced);
}

nlohmann::json controller_only_status() {
    return {{"role", "controller-only"}, {"controller_ready", controller_only_ready()},
        {"device_id", controller_identity}, {"local_output", nullptr},
        {"local_transmission_available", false}};
}

bool controller_http_allowed(std::string_view method, std::string_view path) {
    if (!controller_only_mode()) return true;
    if (method == "GET" && (path == "/" || path == "/version" || path == "/status" ||
        path == "/api/control/v1/status")) return true;
    if (!controller_only_ready()) return false;
    if((method=="GET"&&wsprrypi::local_pairing_read_path(path))||
        (method=="POST"&&wsprrypi::local_pairing_mutation_path(path))||wsprrypi::pi_pair_bootstrap_path(method,path))return true;
    if(path=="/api/control/v1/targets"||path.starts_with("/api/control/v1/targets/"))
        return method=="GET"||method=="PUT"||method=="POST"||method=="DELETE";
    if(path=="/api/recovery/session")return method=="POST";
    if(path=="/api/control/v1/host/management-session")return method=="POST";
    if (method == "GET" && (path == "/api/control/v1/host/devices" ||
        path == "/api/control/v1/host/discovery" || path == "/api/control/v1/host/fleet" ||
        path == "/api/control/v1/host/retirements" || path == "/api/control/v1/host/fleet/operations" || path == "/api/control/v1/host/fleet/actions")) return true;
    return method == "POST" && (path == "/api/control/v1/host/devices" ||
        path == "/api/control/v1/host/discovery/identify" || path == "/api/control/v1/host/retirements" || path == "/api/control/v1/host/management-session" || path == "/api/control/v1/host/fleet");
}

bool controller_only_loop(const std::string &fixture_identity) {
    if (!controller_only_mode()) throw std::logic_error("Controller role was not selected");
    bool result = false;
    std::shared_ptr<wsprrypi::PiPairOwner> pairing_owner;
    const char *stage = "identity";
    try {
        verify_controller_identity=fixture_identity.empty();identity_fenced.store(false);
        controller_identity = fixture_identity.empty() ? wsprrypi::wtp_pi_device_id() : fixture_identity;
        if (controller_identity.size() != 32 || controller_identity.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Controller identity is invalid");
        if(controller_boot.empty()){wsprrypi::WtpPiBootIdentity boot;controller_boot=boot.new_boot_id();}
        // Retain the real production role's private owner through web/Fleet
        // shutdown. Explicit-identity fixtures never select host private state.
        if(fixture_identity.empty())try{pairing_owner=wsprrypi::prepare_pi_pair_owner(controller_identity);}catch(...){/* Independent pairing hold. */}
        stage = "catalog";
        const auto file_catalog=wsprrypi::application_database_selected()?std::unique_ptr<wsprrypi::WtpCatalog>{}:
            std::make_unique<wsprrypi::WtpCatalog>(config.ini_filename+".wtp-devices.json");
        auto &catalog=wsprrypi::application_database_selected()?controller_storage_catalog():*file_catalog;
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
        if (config.enable_web && config.enable_http&&!wsprrypi::application_database_selected()) {
            stage = "HTTP listener";
            webServer.start(config.web_port);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!webServer.isListening() && !exiting_wspr.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!webServer.isListening()) throw std::runtime_error("Controller HTTP listener unavailable");
        }
        stage = "WebSocket listener";
        if (config.enable_web&&!wsprrypi::application_database_selected() && (config.socket_port<1||config.socket_port>65535||!socketServer.start(static_cast<std::uint16_t>(config.socket_port), SOCKET_KEEPALIVE,
                config.socket_loopback_only, config.socket_loopback_family)))
            throw std::runtime_error("Controller WebSocket listener unavailable");
        controller_ready.store(true, std::memory_order_release);
        llog.logS(INFO, "Controller ready; no local transmitter output. Listener settings require process restart.");
        std::unique_lock lock(exitwspr_mtx);
        while(!exitwspr_ready){
            exitwspr_cv.wait_for(lock,std::chrono::milliseconds(100),[]{return exitwspr_ready;});
            if(!controller_only_ready()){
                llog.logS(ERROR,"Controller identity is unavailable; management admission has stopped.");
                break;
            }
        }
        result = exitwspr_ready;
    } catch (const std::exception &) {
        // Do not relay private profile contents or filesystem paths to browsers.
        llog.logS(ERROR, "Controller startup failed at ", stage, "; inspect controller identity, catalog, recovery state, and listeners.");
    }
    controller_ready.store(false, std::memory_order_release);
    webServer.stop();
    socketServer.stop();
    wsprrypi::stop_backend_services();
    wsprrypi::stop_wtp_fleet();
    return result;
}
