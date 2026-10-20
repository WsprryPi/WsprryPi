#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path);
    assert(input.is_open());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}
}

int main() {
    const auto root = std::filesystem::current_path();
    const auto parser = read_file(root / "arg_parser.cpp");
    const auto http = read_file(root / "web_server.cpp") +
                      read_file(root / "web_server_admin_routes.cpp") +
                      read_file(root / "web_server_request_guard.cpp");
    const auto websocket = read_file(root / "web_socket.cpp");
    const auto scheduling = read_file(root / "scheduling.cpp") +
                            read_file(root / "scheduling_runtime.cpp");
    const auto stock_ini = read_file(root / "../config/wsprrypi.ini");

    assert(parser.find("initialize_privileged_network_runtime(") != std::string::npos);
    assert(http.find("active_privileged_network_mode()") != std::string::npos);
    assert(websocket.find("active_privileged_network_mode()") != std::string::npos);
    assert(http.find("server.Get(") != std::string::npos);
    assert(http.find("server.Post(") != std::string::npos);
    assert(http.find("\"/api/network-safety\"") != std::string::npos);
    assert(http.find("Access-Control-Allow-Origin\", \"*\"") == std::string::npos);
    assert(scheduling.find("reconcile_privileged_network_policy(") == std::string::npos);
    assert(scheduling.find("PrivilegedNetworkStartupRecovery") == std::string::npos);
    assert(scheduling.find("HTTP listener started on port") != std::string::npos);
    assert(scheduling.find("HTTP listener failed to bind or listen") !=
           std::string::npos);
    assert(scheduling.find("socketServer.start(") != std::string::npos);
    assert(scheduling.find("no eligible LAN") != std::string::npos);
    assert(stock_ini.find("[Security]") != std::string::npos);
    assert(stock_ini.find("Privileged Network Safety = enforced") != std::string::npos);

    std::cout << "privileged_network_runtime_wiring_test: PASS\n";
}
