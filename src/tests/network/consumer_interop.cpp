// SPDX-License-Identifier: MIT
// Deterministic actual Pi coordinator/OpenSSL -> Pico ConsumerRadioApi/Mbed
// TLS. Transport is in-process; this does not qualify lwIP routing or hardware.
#include "mbedtls/platform.h"
#include "network/consumer_radio.hpp"
#include "network/bootstrap_codec.hpp"
#include "network/pico/consumer_radio_crypto.hpp"
#include "network_support.hpp"
#include "provisioning/consumer_profile.hpp"
#include "wtp_integration/application.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/configuration.hpp"
#include "wtp_runtime_bridge.hpp"
#include "privileged_network_policy.hpp"
#include "protected_browser_fixture.hpp"
#include "wtp_integration/consumer_radio.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <openssl/rand.h>
#include <random>
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) { return 1000; }
extern "C" int mbedtls_hardware_poll(void *, unsigned char *out,
                                     std::size_t count, std::size_t *received) {
  if (RAND_bytes(out, count) != 1)
    return -1;
  *received = count;
  return 0;
}
namespace {
using Json = nlohmann::json;
namespace pico = wsprrypico;
namespace pi = wsprrypi;
const std::string device(32, 'a');
struct Media : pico::provisioning::Media {
  std::array<std::uint8_t, 16384> data{};
  Media() { data.fill(255); }
  bool read(std::size_t at, std::span<std::uint8_t> out) override {
    std::copy_n(data.begin() + at, out.size(), out.begin());
    return true;
  }
  bool erase(std::size_t at) override {
    std::fill_n(data.begin() + at, 8192, 255);
    return true;
  }
  bool program(std::size_t at, std::span<const std::uint8_t> in) override {
    std::copy(in.begin(), in.end(), data.begin() + at);
    return true;
  }
};
struct Random : pico::provisioning::RandomSource {
  bool fill(std::span<std::uint8_t> out) override {
    return RAND_bytes(out.data(), out.size()) == 1;
  }
};
struct HostClock : pi::WtpScheduleClock {
  std::uint64_t now = 1000;
  std::uint64_t now_ms() const override { return now; }
  std::optional<std::uint64_t> utc_now_ns() const override {
    return 1800000000000000000ULL;
  }
  void wait_ms(std::uint64_t ms) override { now += ms; }
};
struct NoWtp : pi::wtp::ByteStream {
  unsigned io{};
  pi::wtp::IoResult read(std::span<std::uint8_t>) override {
    ++io;
    return {pi::wtp::IoState::Failed};
  }
  pi::wtp::IoResult write(std::span<const std::uint8_t>) override {
    ++io;
    return {pi::wtp::IoState::Failed};
  }
  void close() noexcept override {}
};
struct FixtureEngine : pico::wtp::RfEngine {
  pico::standalone::DryRunEngine dry;
  bool can_disable=true;
  unsigned disables{},prepares{};
  pico::wtp::PrepareResult prepare(const pico::wtp::Job &j) override { ++prepares;return dry.prepare(j); }
  bool schedules_locally() const override { return true; }
  bool schedule(const pico::wtp::Job &j,std::uint64_t n,const pico::wtp::LocalStartConditions &c) override {return dry.schedule(j,n,c);}
  bool begin(const pico::wtp::Job &j,std::uint64_t n) override {return dry.begin(j,n);}
  pico::wtp::EngineReport poll(std::uint64_t n) override {return dry.poll(n);}
  bool disable(std::uint64_t n) override {++disables;return can_disable&&dry.disable(n);}
  bool output_active() const override {return false;}
};
struct ResourceFixture {
  network_test::Flash flash;
  pico::standalone::Store store{flash};
  network_test::Clock clock;
  FixtureEngine engine;
  network_test::Identity identities;
  pico::wtp::JobService service{clock,engine,identities,network_test::Fixture::physical_policy()};
  pico::standalone::Scheduler scheduler{store,service};
  network_test::Network network;
  pico::network::BrowserApi api{service,store,scheduler,network,device,"test-firmware"};
  ResourceFixture(){assert(store.load());}
};
struct Test {
  ResourceFixture f;
  Media media;
  Random random;
  HostClock host;
  NoWtp wtp;
  pico::provisioning::ProfileStore profiles{media};
  pico::provisioning::RuntimeProfile runtime;
  pico::network::PicoConsumerRadioCrypto crypto;
  pico::network::ConsumerRadioApi radio{f.api,
                                        profiles,
                                        runtime,
                                        random,
                                        crypto,
                                        device,
                                        f.service.status().boot_id};
  WtpSettings settings;
  std::unique_ptr<pi::WtpApplication> app;
  std::string authority = "192.168.1.53", origin = "http://192.168.1.53";
  pico::network::ConsumerInterface interface =
      pico::network::ConsumerInterface::Station;
  bool drop = false, drop_mutation = false, tamper = false, replay = false;
  unsigned mutations{}, exchanges{};
  std::string previous_packet;
  std::uint64_t transaction = 1;
  explicit Test(bool configured=true) {
    pico::provisioning::ConsumerProfile profile;
    profile.device_id = device;
    profile.ssid = "test-network";
    profile.password = "test-password";
    profile.time_server = "192.0.2.1";
    profile.callsign = "AA0NT";
    profile.locator = "EM18";
    profile.power_dbm = 37;
    profile.tls_pending = true;
    profile.tls.hostname = "wsprrypico-0a60df.local";
    profile.request_sha256 = std::string(64, 'a');
    assert(profiles.load() &&
           profiles.select(
               pico::provisioning::ProfileSource::ConsumerProfile,
               pico::provisioning::serialize_consumer_profile(profile)));
    assert(runtime.load(profiles, device,
                        pico::provisioning::BuildBundleState::Absent));
    f.api.consumer_config(runtime);
    if (configured) {
      assert(f.scheduler.command("CONFIG " + network_test::config).find("true") != std::string::npos);
      assert(f.store.reserve(1791970000000000000ULL));
    }
    radio.admission(true);
    radio.listeners(true, "192.168.1.53", "wsprrypico-0a60df.local");
    f.api.management_availability({pico::network::ManagementProfile::Consumer,
                                   profiles.sequence(), true, false, false,
                                   true, true});
    settings.device_id = device;
    app = std::make_unique<pi::WtpApplication>(
        host, wtp, settings,
        pi::wtp::SessionOptions{std::string(32, '1'), std::string(32, '2'),
                                device},
        [] { return false; });
  }
  auto client() {
    return std::make_unique<pi::ConsumerRadioClient>(
        [this](auto path, auto method, auto body, auto media) {
          return send(path, method, body, media);
        },
        [this] { return host.now; });
  }
  pi::PicoHttpResponse send(const std::string &path, const std::string &method,
                            std::string body, const std::string &type) {
    ++exchanges;
    pico::network::HttpRequest r{method,
                                 path,
                                 {},
                                 {{"host", authority},
                                  {"origin", origin},
                                  {"content-type", type},
                                  {"x-wsprrypico-radio", "1"}}};
    if (type == "application/octet-stream") {
      if (replay && !previous_packet.empty()) {
        body = previous_packet;
        replay = false;
      } else
        previous_packet = body;
      r.buffered_body = std::make_shared<pico::wtp::FrameBuffer>();
      assert(r.buffered_body->reserve(body.size()));
      r.buffered_body->append(pico::wtp::InputView(body));
    } else
      r.body = body;
    const auto before = f.store.config_sequence();
    const auto tx = transaction++;
    auto response = radio.handle(r, host.now, tx, interface);
    if (f.store.config_sequence() != before) {
      ++mutations;
      if (drop_mutation) {
        drop = true;
        drop_mutation = false;
      }
    }
    auto output = response.body_text();
    radio.finish_request(tx, !drop);
    if (drop && path == "/api/consumer/v1/request") {
      drop = false;
      return {503, R"({"error":{"code":"management_outcome_unknown"}})", {}};
    }
    if (tamper && type == "application/octet-stream" && !output.empty()) {
      output.back() ^= 1;
      tamper = false;
    }
    return {response.status, output, response.etag, response.type};
  }
  pi::PicoHttpResponse coordinated(pi::ConsumerRadioClient &c,
                                   const std::string &method = "GET",
                                   const std::string &body = {},
                                   const std::string &revision = {}) {
    pi::PicoHttpResponse result;
    assert(app->idle_management(
        [&] {
          result = pi::pico_configuration_request(
              [&](auto resource, auto verb, auto payload, auto etag) {
                return c.request(resource, verb, payload, etag);
              },
              c.device(), c.boot(), method, body, revision,
              interface == pico::network::ConsumerInterface::Station
                  ? "consumer_station"
                  : "consumer_ap",
              c.generation());
        },
        true));
    assert(wtp.io == 0);
    return result;
  }
};
void cases(bool ap) {
  Test t;
  if (ap) {
    t.authority = "192.168.4.1";
    t.origin = "http://192.168.4.1";
    t.interface = pico::network::ConsumerInterface::AccessPoint;
  }
  auto c = t.client();
  const auto carrier = ap ? "consumer_ap" : "consumer_station";
  assert(c->login(device, carrier, "test-password").status == 200);
  auto context = t.coordinated(*c);
  assert(context.status == 200);
  auto state = Json::parse(context.body);
  assert(state["config"]["wifi"]["password"].is_null());
  const auto original = *t.f.store.config();
  Json edit = {{"schema", "wsprrypi-pico-settings/1"},
               {"target", state["target"]},
               {"operation", "save"},
               {"config",
                {{"allow_non_amateur_frequency", true},
                 {"allow_unqualified_frequency", true}}}};
  auto saved = t.coordinated(*c, "POST", edit.dump(), context.etag);
  assert(saved.status == 200 && t.mutations == 1);
  assert(t.f.store.config()->password == original.password &&
         t.f.store.watermark() == 1791970000000000000ULL);
  assert(t.f.store.config()->callsign == original.callsign &&
         t.f.store.config()->pins == original.pins);
  const auto seq = t.f.store.config_sequence();
  assert(t.coordinated(*c, "POST", edit.dump(), context.etag).status == 412 &&
         t.f.store.config_sequence() == seq);
  edit["config"] = {{"station", {{"callsign", "K1ABC"}}}};
  auto fresh = t.coordinated(*c);
  assert(t.coordinated(*c, "POST", edit.dump(), fresh.etag).status == 409 &&
         t.mutations == 1);
  auto stranger = t.client();
  assert(stranger->login(device, carrier, "test-password").status == 409);
  assert(c->active());
  assert(c->request("jobs", "POST", "{}").status == 404 && t.mutations == 1);
  t.origin = "http://foreign.local";
  assert(c->request("config", "GET").status != 200 && !c->active());
  assert(t.mutations == 1);
}
void resource_admission_cases() {
  for (unsigned phase=0;phase<5;++phase) {
    Test t; auto c=t.client(); assert(c->login(device,"consumer_station","test-password").status==200);
    const auto request=[&](const std::string &method,const std::string &body={},const std::string &revision={}) {
      pi::PicoHttpResponse result;
      assert(t.app->idle_management([&]{result=pi::pico_resource_request([&](auto p,auto m,auto b,auto e){return c->request(p,m,b,e);},
        c->device(),c->boot(),method=="GET" ? "application" : "hardware",method,body,revision,"consumer_station",c->generation());},true));
      return result;
    };
    const auto loaded=request("GET"); assert(loaded.status==200);
    const auto app=Json::parse(loaded.body);
    const Json edit={{"schema","transmitter-hardware/1"},{"target",app["target"]},{"pins",app["application"]["hardware"]["saved"]}};
    unsigned sequence=0;
    const auto invoke=[&](std::string operation,pico::wtp::RequestBody body) {
      pico::wtp::Request r; r.principal="fixture-owner";r.session_id=std::string(32,'b');
      r.request_id=std::string(31,'0')+std::to_string(++sequence);r.operation=operation;r.body=std::move(body);r.payload_digest[0]=sequence;
      return t.f.service.handle(r);
    };
    assert(invoke("HELLO",pico::wtp::HelloBody{{"WTP/1"}}).ok);
    assert(invoke("CLAIM",pico::wtp::ClaimBody{std::string(32,'b'),60000}).ok);
    if (phase>=1) {
      pico::wtp::Job job{std::string(32,'c'),"rf-events/1","tone",1'000'000'000,{},false};
      assert(job.events.push_back({0,job.total_duration_ns,true,7'040'000'000'000'000ULL}));
      assert(invoke("LOAD",std::move(job)).ok);
    }
    if (phase>=2) assert(invoke("ARM",pico::wtp::ArmBody{std::string(32,'c'),t.f.clock.snapshot().utc_now_ns+3'000'000'000,1000}).ok);
    if (phase==3) {t.f.clock.now+=3'000'000'000;t.f.service.poll();assert(t.f.service.status().state==pico::wtp::State::Running);}
    if (phase==4) {t.f.engine.can_disable=false;(void)invoke("ABORT",pico::wtp::AbortBody{std::string(32,'c')});assert(t.f.service.status().state==pico::wtp::State::Failed);}
    const auto bytes=t.f.flash.bytes;const auto status=t.f.service.status();
    const auto result=request("PUT",edit.dump(),loaded.etag);
    assert(result.status==409 && t.f.flash.bytes==bytes && t.f.service.status()==status && t.mutations==0 && t.wtp.io==0);
  }
}
void interactive(bool ap,bool configured=true) {
  Test t(configured);
  if (ap) { t.authority="192.168.4.1"; t.origin="http://192.168.4.1"; t.interface=pico::network::ConsumerInterface::AccessPoint; }
  set_wtp_runtime_for_test(t.settings,t.host,t.wtp,{std::string(32,'1'),std::string(32,'2'),device},[]{return false;});
  struct Reset { ~Reset() { select_wtp_runtime(std::nullopt); } } reset;
  set_wtp_management_transports_for_test({},[&](auto path,auto method,auto body,auto media){return t.send(path,method,body,media);});
  pi::WtpBrowserApi api([]{return Json::parse(wtp_runtime_json());},wtp_runtime_management,[](auto){return pi::PicoHttpResponse{409,R"({"error":{"code":"not_owner"}})",{}};});
  std::cout<<"READY consumer"<<std::endl;
  std::string line;
  while (std::getline(std::cin,line)) {
    const auto command=pi::strict_browser_json(line);
    if (command.contains("control")) {
      const auto control=command["control"];
      if (control=="drop_mutation_reply") t.drop_mutation=true;
      if (control=="expire") t.host.now+=300001;
      if (control=="advance") t.host.now+=1001;
      if (control=="change_profile") {
        auto profile=*t.runtime.consumer_profile(); profile.request_sha256=std::string(64,'b');
        assert(t.profiles.select(pico::provisioning::ProfileSource::ConsumerProfile,pico::provisioning::serialize_consumer_profile(profile)));
        assert(t.runtime.load(t.profiles,device,pico::provisioning::BuildBundleState::Absent));
        t.f.api.management_availability({pico::network::ManagementProfile::Consumer,t.profiles.sequence(),true,false,false,true,true});
      }
      if (control=="fail_storage") t.f.flash.fail=true;
      if (control=="fail_disable") t.f.engine.can_disable=false;
      if (control=="stop") (void)t.f.scheduler.command("STOP");
      if (control=="dump") {
        pico::standalone::Store reload(t.f.flash); const bool healthy=reload.load();
        auto config=reload.config();
        if (config) config->password.clear();
        std::cout<<Json{{"healthy",healthy},{"config",config ? Json::parse(pico::standalone::serialize_config(*config)) : Json(nullptr)},
          {"generation",std::to_string(reload.config_sequence())},{"cursor",std::to_string(reload.watermark())},
          {"bytes_sha256",pico::network::bootstrap_hex(pico::wtp::sha256(t.f.flash.bytes))},
          {"inhibit",t.f.service.output_inhibited()},{"pending_restart",t.f.scheduler.hardware_restart_required()},
          {"application_error",t.f.scheduler.hardware_application_failed()},{"scheduler",Json::parse(t.f.scheduler.status())},
          {"disables",t.f.engine.disables},{"prepares",t.f.engine.prepares},{"mutations",t.mutations},{"wtp_io",t.wtp.io}}.dump()<<std::endl;
      } else std::cout<<"{}"<<std::endl;
      continue;
    }
    const auto result=protected_browser_fixture(api,command);
    std::cout<<Json{{"status",result.status},{"body",pi::strict_browser_json(result.body)},{"etag",result.etag},
      {"wtp_io",t.wtp.io},{"mutations",t.mutations}}.dump()<<std::endl;
  }
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1])=="--stdio") { interactive(std::string(argv[2]).starts_with("ap"),!std::string(argv[2]).ends_with("-blank")); return 0; }
  assert(argc == 2);
  std::ifstream vector_file(argv[1]);
  Json vectors;
  vector_file >> vectors;
  pi::consumer_radio_fixed_vectors(vectors);
  cases(false);
  cases(true);
  resource_admission_cases();
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "wrong-password").status ==
               401 &&
           t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(std::string(32, 'b'), "consumer_station", "test-password")
                   .status == 401 &&
           t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    t.tamper = true;
    assert(c->request("config", "GET").status != 200 && !c->active() &&
           t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    t.replay = true;
    assert(c->request("config", "GET").status != 200 && !c->active() &&
           t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    t.host.now += 300000;
    assert(c->request("config", "GET").status == 401 && t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    t.authority = "wsprrypico-0a60df.local";
    t.origin = "http://" + t.authority;
    assert(c->request("config", "GET").status != 200 && t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    t.interface = pico::network::ConsumerInterface::AccessPoint;
    assert(c->request("config", "GET").status != 200 && t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    auto context = t.coordinated(*c);
    auto state = Json::parse(context.body);
    Json edit = {{"schema", "wsprrypi-pico-settings/1"},
                 {"target", state["target"]},
                 {"operation", "save"},
                 {"config", {{"allow_unqualified_frequency", true}}}};
    // Drop the actual encrypted mutation reply after the Pico journal commit.
    t.drop_mutation = true;
    const auto r = t.coordinated(*c, "POST", edit.dump(), context.etag);
    assert(r.status != 200 && t.mutations == 1 && !c->active());
    const auto before = t.mutations;
    assert(t.coordinated(*c, "POST", edit.dump(), context.etag).status != 200);
    assert(t.mutations == before); // No replay or alternative strategy.
    t.host.now += 300001;
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    const auto readback = t.coordinated(*c);
    assert(readback.status == 200 && readback.etag != context.etag);
    assert(t.coordinated(*c, "POST", edit.dump(), context.etag).status == 412);
    assert(t.mutations == 1);
    pico::standalone::Store reload(t.f.flash);
    assert(reload.load() && reload.config()->allow_unqualified_frequency &&
           reload.watermark() == t.f.store.watermark());
  }

  {
    Test t;
    const auto good = Json::parse(pico::network::management_contract(
        {pico::network::ManagementProfile::Consumer, 1, true, false, false,
         true, true},
        pico::network::ManagementCarrier::ConsumerStation, device,
        t.f.service.status().boot_id));
    assert(pi::valid_management_contract(good, "consumer_station", device,
                                         t.f.service.status().boot_id, "1"));
    for (unsigned variant = 0; variant < 12; ++variant) {
      auto bad = good;
      if (variant == 0)
        bad["schema"] = "transmitter-management/2";
      if (variant == 1)
        bad["limits"]["update"] = 2048.0;
      if (variant == 2)
        bad["profile_generation"] = "2";
      if (variant == 3)
        bad["target"]["device_id"] = std::string(32, 'b');
      if (variant == 4)
        bad["target"]["boot_id"] = std::string(32, 'b');
      if (variant == 5)
        bad["selected"] = "consumer_ap";
      if (variant == 6)
        bad["carriers"][2]["available"] = false;
      if (variant == 7)
        bad["carriers"][2]["built_in"] = false;
      if (variant == 8)
        bad["operations"]["POST"].push_back("jobs");
      if (variant == 9)
        bad["unexpected"] = true;
      if (variant == 10)
        bad["preclock"] = 1;
      if (variant == 11)
        bad["carriers"][2]["available"] = "true";
      assert(!pi::valid_management_contract(bad, "consumer_station", device,
                                            t.f.service.status().boot_id, "1"));
    }
    assert(t.mutations == 0);
  }
  for (const auto profile : {pico::network::ManagementProfile::Blank,
                             pico::network::ManagementProfile::Recovery,
                             pico::network::ManagementProfile::Unavailable}) {
    Test t;
    t.f.api.management_availability({profile});
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status !=
               200 &&
           !c->active() && t.mutations == 0);
  }
  {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    const auto exchanges = t.exchanges;
    assert(c->request("config/update", "POST", std::string(2049, 'x'), "\"r\"")
               .status == 413);
    for (const auto route :
         {"jobs", "station", "hardware",
          "consumer/setup/api/bootstrap/v1/start", "../config", "config?x"})
      assert(c->request(route, "POST", "{}", "\"r\"").status == 404);
    assert(c->request("config", "DELETE", "{}", "\"r\"").status == 404);
    assert(t.exchanges == exchanges && t.mutations == 0);
    c->logout();
    assert(!c->active() && !t.radio.active(t.host.now) && t.mutations == 0);
  }
  for (bool fail : {false, true}) {
    Test t;
    auto c = t.client();
    assert(c->login(device, "consumer_station", "test-password").status == 200);
    const auto context = t.coordinated(*c);
    const auto state = Json::parse(context.body);
    const Json edit = {{"schema", "wsprrypi-pico-settings/1"},
                       {"target", state["target"]},
                       {"operation", "save"},
                       {"config", {{"allow_unqualified_frequency", true}}}};
    const auto bytes = t.f.flash.bytes;
    if (fail)
      t.f.flash.fail = true;
    else
      t.radio.setup_handler(nullptr, nullptr, nullptr,
                            [](void *) { return true; });
    assert(t.coordinated(*c, "POST", edit.dump(), context.etag).status != 200 &&
           t.mutations == 0);
    assert(t.f.flash.bytes == bytes);
    pico::standalone::Store reload(t.f.flash);
    assert(reload.load() && !reload.config()->allow_unqualified_frequency);
  }
  std::cout << "Actual OpenSSL Consumer-Radio/1 station/AP, coordinator, Mbed "
               "TLS, journal and management-only admission passed\n";
}
