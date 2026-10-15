// SPDX-License-Identifier: MIT
// Test-only line protocol drives real browser/compatibility/HTTP adapters.
#include "wtp_integration/application.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/configuration.hpp"
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
    WtpApplication app(clock,tls,settings,{std::string(32,'1'),std::string(32,'2'),settings.device_id},open);
    if (!app.inspect().ok) throw std::runtime_error("Initial WTP negotiation failed");
    nlohmann::json exchanges=nlohmann::json::array();
    bool drop=false;
    WtpBrowserApi api([&]{return wtp_runtime_status_json(app.status());},
      [&](auto resource,auto method,auto body,auto revision) {
        PicoHttpResponse result;
        if (!app.idle_management([&] {
          const auto exchange=[&](const std::string &path,const std::string &verb,const std::string &payload,const std::string &etag) {
            exchanges.push_back({{"resource",path},{"method",verb},{"body",payload},{"revision",etag}});
            auto response=pico_http_request(tls,selection,credentials,[&]{return clock.now_ms();},path,verb,payload,etag);
            if (drop && verb!="GET") { drop=false; return PicoHttpResponse{503,R"({"error":{"code":"management_outcome_unknown"}})",{}}; }
            return response;
          };
          if (resource=="member/configuration") {
            const auto identity=app.status().identity;
            result=pico_configuration_request(exchange,identity?identity->device_id:"",identity?identity->boot_id:"",method,body,revision);
          } else result=exchange(resource,method,body,revision);
        })) return PicoHttpResponse{409,R"({"error":{"code":"host_busy_or_output_unresolved"}})",{}};
        return result;
      },[](auto){return PicoHttpResponse{409,R"({"error":{"code":"not_owner"}})",{}};});
    std::string line;
    while (std::getline(std::cin,line)) {
      const auto command=strict_browser_json(line); exchanges=nlohmann::json::array();
      if (command.contains("control")) { drop=command["control"]=="drop_mutation_reply"; std::cout<<"{}"<<std::endl; continue; }
      const auto response=api.handle({command.at("method"),command.at("path"),command.value("body",""),command.value("revision",""),true});
      std::cout<<nlohmann::json{{"status",response.status},{"body",strict_browser_json(response.body)},
          {"etag",response.etag},{"exchanges",exchanges}}.dump()<<std::endl;
    }
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
