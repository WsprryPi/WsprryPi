// SPDX-License-Identifier: MIT
// Test-only line protocol drives real browser/compatibility/HTTP adapters.
#include "wtp_integration/application.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/configuration.hpp"
#include "wtp_runtime_bridge.hpp"
#include "privileged_network_policy.hpp"
#include "tests/network/protected_browser_fixture.hpp"
#include <iostream>
#include <thread>
using namespace wsprrypi;
namespace {
struct Resolver : TlsResolver {
  bool begin(const std::string &, unsigned) override { return true; }
  auto poll() -> std::optional<std::vector<std::string>> override { return std::vector<std::string>{"127.0.0.1"}; }
  void cancel() noexcept override {}
};
struct Clock : WtpScheduleClock {
  std::uint64_t now_ms() const override { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  std::optional<std::uint64_t> utc_now_ns() const override { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
  void wait_ms(std::uint64_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
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
    const auto open = [&] {
      if (!tls.begin_open(selection,credentials)) return false;
      while (tls.opening()) { tls.poll_open(); clock.wait_ms(1); }
      return tls.ready();
    };
    WtpSettings settings; settings.device_id=std::string(32,'a'); settings.start_uncertainty_ns=1000000;
    settings.transport="network"; settings.hostname=selection.host; settings.tcp_port=selection.port;
    settings.tls_ca=selection.ca_file; settings.tls_certificate=selection.certificate_file; settings.tls_key=selection.key_file;
    set_wtp_runtime_for_test(settings,clock,tls,{std::string(32,'1'),std::string(32,'2'),settings.device_id},open);
    struct Reset { ~Reset() { select_wtp_runtime(std::nullopt); } } reset;
    if (!wtp_runtime_inspect().ok) throw std::runtime_error("Initial WTP negotiation failed");
    nlohmann::json exchanges=nlohmann::json::array();
    bool drop=false; nlohmann::json corruption;
    set_wtp_management_transports_for_test([&](const std::string &path,const std::string &verb,const std::string &payload,const std::string &etag) {
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
    });
    WtpBrowserApi api([]{return nlohmann::json::parse(wtp_runtime_json());},wtp_runtime_management,
      [](auto){return PicoHttpResponse{409,R"({"error":{"code":"not_owner"}})",{}};});
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
          {"etag",response.etag},{"exchanges",exchanges},{"host_ready",wtp_runtime_ready()}}.dump()<<std::endl;
    }
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
