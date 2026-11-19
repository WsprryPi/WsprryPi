#include "udp_logging/management.hpp"
// SPDX-License-Identifier: MIT
// Test-only line protocol drives real browser/compatibility/HTTP adapters.
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/configuration.hpp"
#include "privileged_network_policy.hpp"
#include "tests/network/protected_browser_fixture.hpp"
#include <iostream>
#include <thread>
using namespace wsprrypi;
namespace {
struct Resolver : TlsResolver {
  bool begin(const std::string &, unsigned) { return true; }
  auto poll() -> std::optional<std::vector<std::string>> override { return std::vector<std::string>{"127.0.0.1"}; }
  void cancel() noexcept override {}
};
struct Clock {
  std::uint64_t now_ms() const { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  std::optional<std::uint64_t> utc_now_ns() const { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
  void wait_ms(std::uint64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  try {
    Clock clock;
    const std::string directory = argv[1];
    TlsSelection selection{"wsprrypico-" + std::string(32,'a') + ".local", "", directory+"/client-ca.crt",
        directory+"/client.crt", directory+"/client.key", 18443};
    auto credentials = std::make_shared<TlsCredentials>(selection);
    TlsStream tls([&]{return clock.now_ms();},TlsStream::Access::LoopbackTest,std::make_unique<Resolver>());
    const std::string device(32,'a');
    const auto status=pico_http_request(tls,selection,credentials,[&]{return clock.now_ms();},"status","GET",{},{});
    if(status.status!=200)throw std::runtime_error("Initial native management status unavailable");
    const auto observed=strict_browser_json(status.body);
    const auto boot=observed.at("job").at("boot_id").get<std::string>();
    if(!pico_native_status_valid(observed,device,boot))throw std::runtime_error("Initial native status invalid");
    nlohmann::json exchanges=nlohmann::json::array();
    bool drop=false; nlohmann::json corruption;
    const PicoManagementExchange exchange=[&](const std::string &path,const std::string &verb,const std::string &payload,const std::string &etag) {
      exchanges.push_back({{"resource",path},{"method",verb},{"body",payload},{"revision",etag}});
      auto result=pico_http_request(tls,selection,credentials,[&]{return clock.now_ms();},path,verb,payload,etag);
      if (drop && verb!="GET") { drop=false; return PicoHttpResponse{503,R"({"error":{"code":"management_outcome_unknown"}})",{}}; }
      if (corruption.is_object() && corruption.value("resource","") == path && result.status == 200 && verb == corruption.value("method","GET")) {
        auto value=strict_browser_json(result.body);
        value[nlohmann::json::json_pointer(corruption.at("pointer").get<std::string>())]=corruption.at("value");
        if (corruption.value("as_float",false)) { auto &slot=value[nlohmann::json::json_pointer(corruption.at("pointer").get<std::string>())];slot=slot.get<double>(); }
        result.body=value.dump();
        if (corruption.value("duplicate",false)) result.body.insert(1,"\"schema\":null,");
        if (corruption.value("oversized",false)) result.body.append(4097,' ');
      }
      return result;
    };
    unsigned management_generation=1;
    WtpBrowserApi api([]{return nlohmann::json{{"selected",false},{"ready",false}};},
      [&](auto resource,auto method,auto body,auto revision,auto selected) {
        const std::string context="independent-management-"+std::to_string(management_generation);
        if(resource=="member/management"&&method=="POST") {
          const auto input=strict_browser_json(body);
          if(input==nlohmann::json{{"action","engineering_mtls"}}){tls.close();++management_generation;return PicoHttpResponse{200,R"({"ok":true})",{}};}
          return PicoHttpResponse{400,R"({"error":{"code":"invalid_management_selection"}})",{}};
        }
        if(resource=="member/management"&&method=="GET")return PicoHttpResponse{200,nlohmann::json{{"carrier","engineering_mtls"},{"selection_id",context},{"connected",true}}.dump(),{}};
        if(!selected.empty()&&selected!=context)return PicoHttpResponse{409,R"({"error":{"code":"connection_changed"}})",{}};
        if(resource.starts_with("member/"))resource=resource.substr(7);
        if(resource=="network")return exchange(resource,method,body,revision);
        if(resource.starts_with("udp-logging"))return pico_udp_logging_request(exchange,device,boot,resource,method,body,revision);
        if(resource=="configuration")return pico_configuration_request(exchange,device,boot,method,body,revision,"engineering_mtls");
        if(resource=="progress"||resource=="progress/update"||resource=="progress/apply")return pico_progress_request(exchange,device,boot,resource,method,body,revision,"engineering_mtls");
        return pico_resource_request(exchange,device,boot,resource,method,body,revision,"engineering_mtls");
      },[](auto){return PicoHttpResponse{404,R"({"error":{"code":"unsupported_resource"}})",{}};});
    std::string line;
    while (std::getline(std::cin,line)) {
      const auto command=strict_browser_json(line); exchanges=nlohmann::json::array();
      if (command.contains("control")) {
        drop=command["control"]=="drop_mutation_reply";
        corruption=command.value("corruption",nlohmann::json(nullptr));
        std::cout<<"{}"<<std::endl; continue;
      }
      const auto response=protected_browser_fixture(api,command);
      std::cout<<nlohmann::json{{"status",response.status},{"body",strict_browser_json(response.body)},
          {"etag",response.etag},{"exchanges",exchanges},{"host_ready",false}}.dump()<<std::endl;
    }
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
