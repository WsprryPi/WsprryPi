#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input.is_open());
    return {std::istreambuf_iterator<char>(input), {}};
}
}

int main() {
    const auto root = std::filesystem::current_path();
    const auto service = read_file(root / "rp1_gpclk_route_service.cpp");
    const auto service_header = read_file(root / "rp1_gpclk_route_service.hpp");
    const auto http = read_file(root / "web_server.cpp") +
                      read_file(root / "web_server_admin_routes.cpp");
    const auto scheduling = read_file(root / "scheduling.cpp") +
                            read_file(root / "scheduling_config.cpp") +
                            read_file(root / "scheduling_test_tone.cpp") +
                            read_file(root / "scheduling_runtime.cpp") +
                            read_file(root / "non_wspr_request_builder.cpp") +
                            read_file(root / "rp1_route_bridge.cpp");
    const auto arg_parser = read_file(root / "arg_parser.cpp");
    const auto transmit = read_file(
        root / "WSPR-Transmitter/src/rp1_gpclk_transmit_backend.cpp");
    const auto development_contract = read_file(
        root / "WSPR-Transmitter/src/rp1_gpclk_development_policy.hpp");
    const auto installer = read_file(root / "../scripts/install.sh");
    assert(service.find("/run/rp1-gpclk-dkms/route-manager.sock") != std::string::npos);
    assert(service.find("/usr/bin/systemd-run") != std::string::npos);
    assert(service.find("\"--no-block\"") != std::string::npos);
    assert(service.find("\"--collect\"") != std::string::npos);
    assert(service.find("RuntimeMaxSec=90s") != std::string::npos);
    assert(service.find("rp1-gpclk-route-manager") != std::string::npos);
    assert(service.find("SOCK_STREAM") != std::string::npos && service.find("FD_CLOEXEC") != std::string::npos);
    assert(service.find("{\"eligible\", false}") != std::string::npos);
    assert(service.find("apply-and-reboot") != std::string::npos);
    assert(service.find("packageIdentity") == std::string::npos);
    assert(development_contract.find("kRp1GpclkDevelopmentSourceRevision") == std::string::npos);
    assert(service.find("642793e04268ddb06e35f16249d09c98e4067acef93c62620307bbea50033f5a") == std::string::npos);
    assert(service.find("/boot/firmware/config.txt") == std::string::npos);
    assert(service.find("atomic_write_owned_fragment") == std::string::npos);
    assert(service.find("reboot_system()") == std::string::npos);
    assert(http.find("server.Get(") != std::string::npos);
    assert(http.find("server.Post(") != std::string::npos);
    assert(http.find("\"/api/rp1-gpclk-route\"") != std::string::npos);
    const auto idle_startup = scheduling.find("reconcile_rp1_idle_startup(");
    const auto bounded_request = scheduling.find("if (tone_request.rp1_development.enabled)");
    const auto development_reconciliation =
        scheduling.find("apply_test_tone_rp1_development_confirmation_bridge(", bounded_request);
    const auto request_commit =
        scheduling.find("commit_execution_request(request)", bounded_request);
    const auto listener_start =
        scheduling.find("const bool start_web =", idle_startup);
    assert(idle_startup != std::string::npos);
    assert(bounded_request != std::string::npos);
    assert(development_reconciliation != std::string::npos);
    assert(request_commit != std::string::npos);
    assert(listener_start != std::string::npos);
    assert(idle_startup < listener_start);
    const auto restoration_ack = scheduling.find("acknowledge_rp1_restoration(");
    const auto listener_ready = scheduling.find("!webServer.isListening()");
    assert(restoration_ack != std::string::npos && listener_ready > listener_start && listener_ready < restoration_ack);
    assert(http.find("return svr && svr->is_running();") != std::string::npos);
    assert(bounded_request < development_reconciliation);
    assert(development_reconciliation < request_commit);
    const auto direct_confirmation = scheduling.find(
        "apply_direct_rp1_development_confirmation(");
    const auto tone_request_builder = scheduling.find(
        "TransmissionRequest make_tone_request(");
    const auto direct_tone_start = scheduling.find(
        "bool start_direct_tone_execution(", tone_request_builder);
    const auto direct_tone_confirmation = scheduling.find(
        "apply_direct_rp1_development_confirmation(", direct_tone_start);
    const auto direct_tone_gate = scheduling.find(
        "direct_tone_inhibition_reason(cfg)", direct_tone_confirmation);
    const auto direct_tone_commit = scheduling.find(
        "commit_execution_request(request)", direct_tone_gate);
    const auto direct_start = scheduling.find(
        "bool start_non_wspr_transmission_now(");
    const auto direct_reconciliation = scheduling.find(
        "apply_direct_rp1_development_confirmation(", direct_start);
    const auto direct_runtime_gate = scheduling.find(
        "runtime_transmit_enabled(cfg)", direct_reconciliation);
    const auto direct_commit = scheduling.find(
        "prepare_and_commit_non_wspr_request(", direct_runtime_gate);
    const auto wspr_request = scheduling.find(
        "request_out = make_wspr_request(");
    const auto wspr_confirmation = scheduling.find(
        "apply_direct_rp1_development_confirmation(", wspr_request);
    const auto wspr_request_return = scheduling.find(
        "return true;", wspr_confirmation);
    const auto scheduler_set_config = scheduling.find(
        "bool set_config(bool force)");
    const auto wspr_preparation_gate = scheduling.find(
        "if (!runtime_transmit_preparation_enabled(working_config))",
        scheduler_set_config);
    const auto scheduler_wspr_configuration = scheduling.find(
        "if (!configure_current_wspr_transmission(", wspr_preparation_gate);
    const auto wspr_post_confirmation_gate = scheduling.find(
        "if (!runtime_transmit_enabled(working_config))",
        scheduler_wspr_configuration);
    const auto scheduler_wspr_commit = scheduling.find(
        "commit_execution_request(next_transmission_request)",
        wspr_post_confirmation_gate);
    assert(arg_parser.find("rp1-development-confirmation-json") != std::string::npos);
    assert(direct_confirmation != std::string::npos);
    assert(tone_request_builder != std::string::npos);
    assert(direct_tone_start != std::string::npos);
    assert(direct_tone_confirmation != std::string::npos);
    assert(direct_tone_gate != std::string::npos);
    assert(direct_tone_commit != std::string::npos);
    assert(direct_tone_start < direct_tone_confirmation);
    assert(direct_tone_confirmation < direct_tone_gate);
    assert(direct_tone_gate < direct_tone_commit);
    assert(direct_start != std::string::npos);
    assert(direct_reconciliation != std::string::npos);
    assert(direct_runtime_gate != std::string::npos);
    assert(direct_commit != std::string::npos);
    assert(direct_start < direct_reconciliation);
    assert(direct_reconciliation < direct_runtime_gate);
    assert(direct_runtime_gate < direct_commit);
    assert(wspr_request != std::string::npos);
    assert(wspr_confirmation != std::string::npos);
    assert(wspr_request_return != std::string::npos);
    assert(wspr_request < wspr_confirmation);
    assert(wspr_confirmation < wspr_request_return);
    assert(scheduler_set_config != std::string::npos);
    assert(wspr_preparation_gate != std::string::npos);
    assert(scheduler_wspr_configuration != std::string::npos);
    assert(wspr_post_confirmation_gate != std::string::npos);
    assert(scheduler_wspr_commit != std::string::npos);
    assert(wspr_preparation_gate < scheduler_wspr_configuration);
    assert(scheduler_wspr_configuration < wspr_post_confirmation_gate);
    assert(wspr_post_confirmation_gate < scheduler_wspr_commit);
    assert(scheduling.find("physical_connection_confirmed") != std::string::npos);
    assert(scheduling.find("confirmation_operation_id") != std::string::npos);
    assert(scheduling.find("? cfg.rp1_gpio_drive_ma") != std::string::npos);
    assert(scheduling.find("rp1GpclkDevelopmentOperationArmedForRoute(route)") == std::string::npos);
    assert(scheduling.find("Legacy transmission output") == std::string::npos);
    assert(scheduling.find("rp1_gpclk_application_idle_state") != std::string::npos);
    assert(scheduling.find("rp1_live_qualification_inhibited") == std::string::npos);
    assert(transmit.find("decideRp1GpclkDevelopmentUse(current_policy)") != std::string::npos);
    assert(scheduling.find("productionRp1GpclkRouteService().query(false)") != std::string::npos);
    assert(scheduling.find("route.eligible ? \"eligible\" : \"unavailable\"") == std::string::npos);
    const std::string stock_vhost = read_file("../config/wsprrypi.conf");
    assert(stock_vhost.find("ProxyPass        /wsprrypi/api/rp1-gpclk-route") != std::string::npos);
    assert(installer.find("ProxyPass        /wsprrypi/api/rp1-gpclk-route") != std::string::npos);
    assert(service.find("std::system(") == std::string::npos && service.find("popen(") == std::string::npos);
    std::cout << "rp1_gpclk_route_runtime_wiring_test: PASS\n";
}
