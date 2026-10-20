#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t count(const std::string &text, const std::string &needle) {
    std::size_t matches = 0;
    for (std::size_t position = text.find(needle); position != std::string::npos;
         position = text.find(needle, position + needle.size())) {
        ++matches;
    }
    return matches;
}
}  // namespace

int main() {
    const std::filesystem::path source_root = std::filesystem::current_path();
    const std::string source = read_file(source_root / "web_server.cpp");
    const std::string header = read_file(source_root / "web_server.hpp");
    const std::string config_routes =
        read_file(source_root / "web_server_config_routes.cpp");
    const std::string config_http =
        read_file(source_root / "web_server_config_http.cpp");
    const std::string control_routes =
        read_file(source_root / "web_server_control_routes.cpp");
    const std::string admin_routes =
        read_file(source_root / "web_server_admin_routes.cpp");
    const std::string admin_http =
        read_file(source_root / "web_server_admin_http.cpp");
    const std::string request_guard =
        read_file(source_root / "web_server_request_guard.cpp");
    const std::string version_routes =
        read_file(source_root / "web_server_version_routes.cpp");
    const std::string support_http =
        read_file(source_root / "support_bundle_http.cpp");
    const std::string support_http_header =
        read_file(source_root / "support_bundle_http.hpp");
    const std::string support_intake_routes =
        read_file(source_root / "support_bundle_http_intake_routes.cpp");
    const std::string support_job_routes =
        read_file(source_root / "support_bundle_http_job_routes.cpp");
    const std::string support_download_routes =
        read_file(source_root / "support_bundle_http_download_routes.cpp");
    const std::string support_query_routes =
        read_file(source_root / "support_bundle_http_query_routes.cpp");
    const std::string route_sources =
        config_routes + config_http + control_routes + admin_routes +
        admin_http + request_guard + version_routes;

    const std::size_t construction = source.find("SupportBundleRuntime::create_production()");
    const std::size_t registration = source.find("register_support_bundle_http_routes(");
    assert(construction != std::string::npos && registration != std::string::npos);
    assert(construction < registration);
    assert(count(source, "SupportBundleRuntime::create_production()") == 1);
    assert(count(source, "register_support_bundle_http_routes(") == 1);
    assert(header.find("network_snapshot_provider_") != std::string::npos);
    assert(source.find("network_snapshot_provider_()") != std::string::npos);
    assert(count(source, "resolve_support_bundle_intake_production") == 1);
    assert(source.find("resolve_support_bundle_intake_production);") != std::string::npos);

    const std::size_t stop = source.find("svr->stop();");
    const std::size_t join = source.find("serverThread.join();");
    const std::size_t shutdown = source.find("supportBundleJobManager_->shutdown();");
    assert(stop != std::string::npos && join != std::string::npos && shutdown != std::string::npos);
    assert(stop < join && join < shutdown);
    assert(header.find("std::unique_ptr<SupportBundleJobManager> supportBundleJobManager_") <
           header.find("std::unique_ptr<httplib::Server> svr"));

    const std::size_t manager_shutdown = source.find("supportBundleJobManager_->shutdown();");
    const std::size_t server_reset = source.find("svr.reset();");
    const std::size_t manager_reset = source.find("supportBundleJobManager_.reset();");
    assert(manager_shutdown < server_reset && server_reset < manager_reset);
    assert(source.find("svr = std::make_unique<httplib::Server>();") != std::string::npos);
    assert(source.find("supportBundleRoutesRegistered_ = false;") != std::string::npos);

    assert(source.find("svr->listen(\"0.0.0.0\", port_);") != std::string::npos);
    assert(count(source, "->listen(") == 1);
    assert(config_routes.find("\"/config\"") != std::string::npos);
    assert(version_routes.find("\"/version\"") != std::string::npos);
    assert(request_guard.find("server.set_pre_routing_handler(") != std::string::npos);
    assert(request_guard.find("evaluate_backend_http_request(") != std::string::npos);
    assert(request_guard.find("BackendHttpGuardDecision::allowed") != std::string::npos);
    assert(source.find("web_server_routes::register_request_guard(") != std::string::npos);
    assert(source.find("web_server_routes::register_config(") != std::string::npos);
    assert(source.find("web_server_routes::register_control(") != std::string::npos);
    assert(source.find("web_server_routes::register_privileged_admin(") != std::string::npos);
    assert(source.find("web_server_routes::register_version(") != std::string::npos);
    assert(count(config_routes, "\"/config\"") == 3);
    assert(count(control_routes, "\"/control/stop\"") == 1);
    assert(count(admin_routes, "\"/api/network-safety\"") == 2);
    assert(count(admin_routes, "\"/api/rp1-gpclk-route\"") == 2);
    assert(count(version_routes, "\"/version\"") == 1);
    assert(config_routes.find("json.hpp") == std::string::npos);
    assert(control_routes.find("json.hpp") == std::string::npos);
    assert(admin_routes.find("json.hpp") == std::string::npos);
    assert(config_http.find("httplib.hpp") == std::string::npos);
    assert(admin_http.find("httplib.hpp") == std::string::npos);
    const std::size_t mutation_handler =
        config_routes.find("const auto handle_put_patch");
    const std::size_t mutation_registration = config_routes.find("server.Put(\"/config\"");
    assert(mutation_handler != std::string::npos &&
           mutation_registration != std::string::npos &&
           mutation_handler < mutation_registration);
    assert(config_routes.substr(
               mutation_handler, mutation_registration - mutation_handler)
               .find("set_cors_headers(response)") == std::string::npos);
    const std::size_t update_builder =
        config_http.find("RouteResponse apply_config_update(");
    const std::size_t repair_builder =
        config_http.find("RouteResponse apply_config_repair(");
    assert(update_builder != std::string::npos &&
           repair_builder != std::string::npos &&
           update_builder < repair_builder);
    assert(config_http.substr(update_builder, repair_builder - update_builder)
               .find("true};") == std::string::npos);
    assert(route_sources.find("create_directory") == std::string::npos);
    assert(route_sources.find("supportBundleJobManager_->create(") == std::string::npos);

    const std::size_t intake_routes =
        support_http.find("register_intake_routes(");
    const std::size_t job_routes =
        support_http.find("register_job_mutation_routes(");
    const std::size_t download_routes =
        support_http.find("register_download_routes(");
    const std::size_t query_routes =
        support_http.find("register_query_routes(");
    assert(intake_routes != std::string::npos &&
           job_routes != std::string::npos &&
           download_routes != std::string::npos &&
           query_routes != std::string::npos);
    assert(intake_routes < job_routes &&
           job_routes < download_routes &&
           download_routes < query_routes);
    assert(count(support_http, "register_intake_routes(") == 1);
    assert(count(support_http, "register_job_mutation_routes(") == 1);
    assert(count(support_http, "register_download_routes(") == 1);
    assert(count(support_http, "register_query_routes(") == 1);
    assert(count(support_intake_routes, "\"/api/support-intake\"") == 2);
    assert(count(support_job_routes, "\"/api/support-bundles\"") == 1);
    assert(count(support_download_routes, "/download)") == 1);
    assert(count(support_query_routes, "\"/api/support-bundles\"") == 2);
    assert(support_http_header.find("support_bundle_http_internal") ==
           std::string::npos);

    std::cout << "support_bundle_web_server_wiring_test: PASS\n";
}
