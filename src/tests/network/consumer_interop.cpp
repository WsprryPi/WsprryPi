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
#include "../wtp_backend_peer.hpp"
#include "wtp_integration/consumer_radio.hpp"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <sys/wait.h>
#include <unistd.h>
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
  bool fail=false;
  bool fill(std::span<std::uint8_t> out) override {
    return !fail && RAND_bytes(out.data(), out.size()) == 1;
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
  bool can_disable=true, failure_report=false, reported_active=false;
  unsigned disables{},prepares{};
  pico::wtp::PrepareResult prepare(const pico::wtp::Job &j) override { ++prepares;return dry.prepare(j); }
  bool schedules_locally() const override { return true; }
  bool schedule(const pico::wtp::Job &j,std::uint64_t n,const pico::wtp::LocalStartConditions &c) override {return dry.schedule(j,n,c);}
  bool begin(const pico::wtp::Job &j,std::uint64_t n) override {return dry.begin(j,n);}
  pico::wtp::EngineReport poll(std::uint64_t n) override {
    return failure_report ? pico::wtp::EngineReport{pico::wtp::EngineState::Failed,reported_active,{}} : dry.poll(n);
  }
  bool disable(std::uint64_t n) override {++disables;return can_disable&&dry.disable(n);}
  bool output_active() const override {return reported_active;}
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
#ifdef WSPRRY_PICO_MULTI_SESSIONS
  pico::network::PicoConsumerRadioCrypto crypto[4];
#else
  pico::network::PicoConsumerRadioCrypto crypto;
#endif
  pico::network::ConsumerRadioApi radio{f.api,
                                        profiles,
                                        runtime,
                                        random,
#ifdef WSPRRY_PICO_MULTI_SESSIONS
                                        {&crypto[0], &crypto[1], &crypto[2]},
#else
                                        crypto,
#endif
                                        device,
                                        f.service.status().boot_id
#ifdef WSPRRY_PICO_RECOVERY
                                        , &crypto[3]
#endif
                                        };
  WtpSettings settings;
  std::unique_ptr<pi::WtpApplication> app;
  std::string authority = "192.168.1.53", origin = "http://192.168.1.53";
  pico::network::ConsumerInterface interface =
      pico::network::ConsumerInterface::Station;
  bool drop = false, drop_mutation = false, tamper = false, replay = false;
  unsigned mutations{}, exchanges{};
  bool drop_recovery_finish=false, drop_before_recovery_finish=false;
  std::map<std::string,unsigned> handler_calls;
  std::string previous_packet;
  std::uint64_t transaction = 1;
  explicit Test(bool configured=true, std::string callsign="AA0NT", std::string locator="EM18") {
    pico::provisioning::ConsumerProfile profile;
    profile.device_id = device;
    profile.ssid = "test-network";
    profile.password = "test-password";
    profile.time_server = "192.0.2.1";
    profile.callsign = std::move(callsign);
    profile.locator = std::move(locator);
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
    if (drop_before_recovery_finish && path == "/api/consumer/v1/recovery/finish") {
      drop_before_recovery_finish=false;
      return {503,R"({"error":{"code":"reply_lost_before_dispatch"}})",{}};
    }
    ++handler_calls[path];
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
    if (drop_recovery_finish && path == "/api/consumer/v1/recovery/finish") {
      drop_recovery_finish=false;
      return {503,R"({"error":{"code":"reply_lost_after_dispatch"}})",{}};
    }
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
#ifdef WSPRRY_PICO_MULTI_SESSIONS
  t.host.now += 1000;
  assert(stranger->login(device, carrier, "test-password").status == 200);
  for (unsigned i=0;i<3;++i) {
    assert(stranger->request("status","GET").status == 200);
    assert(c->request("status","GET").status == 200);
  }
  auto third=t.client(); t.host.now+=1000;
  assert(third->login(device,carrier,"test-password").status==200);
  auto fourth=t.client(); t.host.now+=1000;
  assert(fourth->login(device,carrier,"test-password").status==409);
  assert(third->request("status","GET").status==200);
  stranger->logout();
  assert(c->request("status","GET").status==200);
#else
  assert(stranger->login(device, carrier, "test-password").status == 409);
#endif
  assert(c->active());
  assert(c->request("jobs", "POST", "{}").status == 404 && t.mutations == 1);
  t.origin = "http://foreign.local";
  assert(c->request("config", "GET").status != 200 && !c->active());
  assert(t.mutations == 1);
}
void recovery_cases() {
#ifdef WSPRRY_PICO_RECOVERY
  Test t; auto a=t.client(),b=t.client(),c=t.client();
  for (auto* client : {a.get(),b.get(),c.get()}) {
    assert(client->login(device,"consumer_station","test-password").status==200);t.host.now+=1000;
  }
  const auto selected=a->lost_session();
  const Json scope={{"target_selector",selected.at("selector")},{"device_id",device},
      {"boot_id",selected.at("boot_id")},{"generation",selected.at("generation")}};
  assert(a->recovery(device,"consumer_station",t.authority).status==200);
  auto fourth=t.client();assert(fourth->login(device,"consumer_station","test-password").status==409);
  assert(b->retire(device,"consumer_station",t.authority,"wrong-password",scope).status!=200);
  for (auto* client : {a.get(),b.get(),c.get()}) assert(client->request("status","GET").status==200);
  t.host.now+=1000;
  assert(b->retire(device,"consumer_station",t.authority,"test-password",scope).status==200);
  assert(a->request("status","GET").status!=200);assert(!a->active());
  for (auto* client : {b.get(),c.get()}) assert(client->request("config","GET").status==200);
  t.host.now+=1000;assert(a->login(device,"consumer_station","test-password").status==200);
  assert(b->retire(device,"consumer_station",t.authority,"test-password",scope).status==200);
  assert(a->request("status","GET").status==200);
  auto stale=scope;stale["boot_id"]=std::string(32,'f');t.host.now+=1000;
  assert(b->retire(device,"consumer_station",t.authority,"test-password",stale).status==409);
  assert(t.mutations==0&&t.wtp.io==0);
#else
  Test t;auto c=t.client();assert(c->login(device,"consumer_station","test-password").status==200);
  assert(c->recovery(device,"consumer_station",t.authority).status!=200);
  assert(c->active()&&c->request("config","GET").status==200);
#endif
}

void alias_fixed_vector(const Json &vector) {
  // The fixed grant enters the production native login path. Losing its finish
  // reply leaves only the public alias, whose expected bytes are independently
  // fixed by Consumer-Recovery/1's selector vector.
  const Json identity={{"version",1},{"device_id",vector.at("device_id")},
      {"boot_id",vector.at("boot_id")},{"generation",vector.at("generation")},
      {"authentication","saved_wifi_passphrase"}};
  const Json grant={{"version",1},{"device_id",vector.at("device_id")},
      {"boot_id",vector.at("boot_id")},{"generation",vector.at("generation")},
      {"session_id",vector.at("session_id")},{"public_key",vector.at("pico_public_key")},
      {"challenge",vector.at("challenge")},{"login_ms",30000}};
  unsigned finishes=0;
  pi::ConsumerRadioClient client([&](auto path,auto method,auto,auto) {
    if (path=="/api/consumer/v1/identity") {
      assert(method=="GET");return pi::PicoHttpResponse{200,identity.dump(),{},"application/json"};
    }
    if (path=="/api/consumer/v1/start") {
      assert(method=="POST");return pi::PicoHttpResponse{200,grant.dump(),{},"application/json"};
    }
    assert(path=="/api/consumer/v1/finish" && method=="POST");++finishes;
    return pi::PicoHttpResponse{503,"",{}};
  },[]{return 1000;});
  assert(client.login(vector.at("device_id"),"consumer_station",vector.at("password").get<std::string>()).status==401);
  assert(finishes==1 && !client.active());
  assert(client.lost_session().at("selector")=="e1adf81f3688e1b7fcc7f1bb034c63ea");
  assert(!client.lost_session().contains("session_id"));
}

void retirement_job_authority_cases() {
#ifdef WSPRRY_PICO_RECOVERY
  for (unsigned phase=0;phase<5;++phase) {
    Test t;auto victim=t.client(),peer=t.client();
    assert(victim->login(device,"consumer_station","test-password").status==200);
    t.host.now+=1000;assert(peer->login(device,"consumer_station","test-password").status==200);
    unsigned sequence=0;
    const auto invoke=[&](const std::string &operation,pico::wtp::RequestBody body) {
      pico::wtp::Request request;request.principal="fixture-owner";request.session_id=std::string(32,'b');
      request.request_id=std::string(31,'0')+std::to_string(++sequence);request.operation=operation;
      request.body=std::move(body);request.payload_digest[0]=sequence;return t.f.service.handle(request);
    };
    assert(invoke("HELLO",pico::wtp::HelloBody{{"WTP/1"}}).ok);
    assert(invoke("CLAIM",pico::wtp::ClaimBody{std::string(32,'b'),60000}).ok);
    pico::wtp::Job job{std::string(32,'c'),"rf-events/1","tone",1'000'000'000,{},false};
    assert(job.events.push_back({0,job.total_duration_ns,true,7'040'000'000'000'000ULL}));
    assert(invoke("LOAD",std::move(job)).ok);
    if (phase>=1) assert(invoke("ARM",pico::wtp::ArmBody{std::string(32,'c'),t.f.clock.snapshot().utc_now_ns+3'000'000'000,1000}).ok);
    if (phase>=2) {
      t.f.clock.now+=3'000'000'000;t.f.service.poll();
      assert(t.f.service.status().state==pico::wtp::State::Running);
    }
    if (phase==3) {t.f.engine.failure_report=true;t.f.service.poll();}
    if (phase==4) {
      t.f.engine.can_disable=false;t.f.engine.reported_active=true;
      assert(!invoke("ABORT",pico::wtp::AbortBody{std::string(32,'c')}).ok);
    }
    const auto before=t.f.service.status();
    const std::array expected{pico::wtp::State::Loaded,pico::wtp::State::Armed,pico::wtp::State::Running,pico::wtp::State::Failed,pico::wtp::State::Failed};
    assert(before.state==expected[phase] && before.owner_id==std::string(32,'b') && before.job_id==std::string(32,'c'));
    if (phase>=3) assert(before.terminal_records.front().error==(phase==3 ? pico::wtp::ErrorCode::DeviceFault : pico::wtp::ErrorCode::OutputStateUnknown));
    const auto flash=t.f.flash.bytes,profile=t.media.data;
    const auto scheduler=t.f.scheduler.status();const auto watermark=t.f.store.watermark();
    const auto disables=t.f.engine.disables,prepares=t.f.engine.prepares;
    const bool inhibit=t.f.service.output_inhibited();
    const auto retained=victim->lost_session();
    const Json scope={{"target_selector",retained.at("selector")},{"device_id",device},
        {"boot_id",retained.at("boot_id")},{"generation",retained.at("generation")}};
    assert(peer->retire(device,"consumer_station",t.authority,"wrong-password",scope).status!=200);
    assert(victim->request("status","GET").status==200 && peer->request("status","GET").status==200);
    t.host.now+=1000;
    assert(peer->retire(device,"consumer_station",t.authority,"test-password",scope).status==200);
    assert(victim->request("status","GET").status!=200 && peer->request("status","GET").status==200);
    assert(t.f.service.status()==before && t.f.flash.bytes==flash && t.media.data==profile);
    assert(t.f.scheduler.status()==scheduler && t.f.store.watermark()==watermark && t.f.service.output_inhibited()==inhibit);
    assert(t.f.engine.disables==disables && t.f.engine.prepares==prepares && t.mutations==0 && t.wtp.io==0);
  }
#endif
}


void local_retirement_refusal_cases() {
#ifdef WSPRRY_PICO_RECOVERY
  Test t;auto native=t.client(),peer=t.client();
  assert(native->login(device,"consumer_station","test-password").status==200);
  t.host.now+=1000;assert(peer->login(device,"consumer_station","test-password").status==200);
  const auto retained=native->lost_session();
  const Json scope={{"target_selector",retained.at("selector")},{"device_id",device},
      {"boot_id",retained.at("boot_id")},{"generation",retained.at("generation")}};
  const auto starts=t.handler_calls["/api/consumer/v1/recovery/start"],finishes=t.handler_calls["/api/consumer/v1/recovery/finish"];
  assert(native->retire(device,"consumer_station",t.authority,"short",scope).status!=200);
  auto malformed=scope;malformed.erase("generation");
  assert(native->retire(device,"consumer_station",t.authority,"test-password",malformed).status!=200);
  assert(native->active() && native->lost_session()==retained && native->request("status","GET").status==200);
  assert(t.handler_calls["/api/consumer/v1/recovery/start"]==starts && t.handler_calls["/api/consumer/v1/recovery/finish"]==finishes);
  t.random.fail=true;
  assert(native->retire(device,"consumer_station",t.authority,"test-password",scope).status!=200);
  t.random.fail=false;
  assert(native->active() && native->request("status","GET").status==200 && peer->request("status","GET").status==200);
  assert(t.handler_calls["/api/consumer/v1/recovery/start"]==starts+1 && t.handler_calls["/api/consumer/v1/recovery/finish"]==finishes);
  assert(t.mutations==0 && t.wtp.io==0);
#endif
}

struct RuntimeReset {
  ~RuntimeReset() {select_wtp_runtime(std::nullopt);}
};
void runtime_recovery_continuity_cases() {
#ifdef WSPRRY_PICO_RECOVERY
  for (bool after_effect : {false,true}) {
    Test t;
    set_wtp_runtime_for_test(t.settings,t.host,t.wtp,{std::string(32,'1'),std::string(32,'2'),device},[]{return false;});
    RuntimeReset reset;
    set_wtp_management_transports_for_test({},[&](auto p,auto m,auto b,auto type){return t.send(p,m,b,type);});
    const auto management=[&](const Json &body) {return wtp_runtime_management("member/management","POST",body.dump(),"");};
    const auto state=[] {return Json::parse(wtp_runtime_management("member/management","GET","","").body);};
    const Json connect={{"action","connect"},{"carrier","consumer_station"},{"host",t.authority},{"password","test-password"}};
    // The native result reached its handler but the browser need not observe it.
    assert(management(connect).status==200);
    const auto native=state();assert(native.at("connected")==true && native.at("session").contains("selector"));

    Json local_self={{"action","retire_login"},{"carrier","consumer_station"},{"host",t.authority},{"password","short"},
        {"selection_id",native.at("selection_id")},{"scope",{{"target_selector",native.at("session").at("selector")},
        {"device_id",device},{"boot_id",native.at("session").at("boot_id")},{"generation",native.at("session").at("generation")}}}};
    const auto proof_starts=t.handler_calls["/api/consumer/v1/recovery/start"],proof_finishes=t.handler_calls["/api/consumer/v1/recovery/finish"];
    assert(management(local_self).status!=200);
    local_self["password"]="test-password";local_self["scope"].erase("generation");
    assert(management(local_self).status!=200);
    assert(state().at("connected")==true && state().at("session")==native.at("session"));
    assert(t.handler_calls["/api/consumer/v1/recovery/start"]==proof_starts && t.handler_calls["/api/consumer/v1/recovery/finish"]==proof_finishes);
    local_self["scope"]["generation"]=native.at("session").at("generation");t.random.fail=true;
    assert(management(local_self).status!=200);t.random.fail=false;
    assert(state().at("connected")==true && wtp_runtime_management("member/configuration","GET","","").status==200);
    assert(t.handler_calls["/api/consumer/v1/recovery/start"]==proof_starts+1 && t.handler_calls["/api/consumer/v1/recovery/finish"]==proof_finishes);
    t.host.now+=1000;

    const auto starts=t.handler_calls["/api/consumer/v1/start"];
    assert(state().at("session")==native.at("session") && t.handler_calls["/api/consumer/v1/start"]==starts);
    auto victim=t.client(),healthy=t.client();
    t.host.now+=1000;assert(victim->login(device,"consumer_station","test-password").status==200);
    t.host.now+=1000;assert(healthy->login(device,"consumer_station","test-password").status==200);
    const Json inspect={{"action","inspect_logins"},{"carrier","consumer_station"},{"host",t.authority}};
    auto inspected=Json::parse(management(inspect).body);assert(inspected.at("sessions").size()==3);
    const auto scope_for=[&](const Json &retained) {
      return Json{{"target_selector",retained.at("selector")},{"device_id",device},
          {"boot_id",retained.at("boot_id")},{"generation",retained.at("generation")}};
    };
    Json retire={{"action","retire_login"},{"carrier","consumer_station"},{"host",t.authority},{"password","test-password"},
        {"selection_id",inspected.at("selection_id")},{"scope",scope_for(victim->lost_session())}};
    assert(management(retire).status==200);
    assert(state().at("connected")==true && state().at("session")==native.at("session"));
    assert(wtp_runtime_management("member/configuration","GET","","").status==200);
    assert(victim->request("status","GET").status!=200 && healthy->request("status","GET").status==200);
    const auto calls=t.exchanges;
    assert(management(retire).status!=200 && t.exchanges==calls); // Old selection cannot dispatch.
    inspected=Json::parse(management(inspect).body);
    retire["selection_id"]=inspected.at("selection_id");retire["scope"]=scope_for(native.at("session"));
    t.host.now+=1000;
    const auto before_finish=t.handler_calls["/api/consumer/v1/recovery/finish"];
    const auto before_starts=t.handler_calls["/api/consumer/v1/start"];
    t.drop_recovery_finish=after_effect;t.drop_before_recovery_finish=!after_effect;
    assert(management(retire).status!=200);
    assert(state().at("connected")==false && state().at("session")==native.at("session"));
    assert(t.handler_calls["/api/consumer/v1/recovery/finish"]==before_finish+(after_effect ? 1U : 0U));
    assert(t.handler_calls["/api/consumer/v1/start"]==before_starts);
    assert(healthy->request("status","GET").status==200);
    const auto list=Json::parse(healthy->recovery(device,"consumer_station",t.authority).body);
    const bool occupied=std::any_of(list.at("sessions").begin(),list.at("sessions").end(),[&](const Json &entry){return entry.at("selector")==native.at("session").at("selector");});
    assert(occupied==!after_effect);
    // Only the dedicated five-second proof is expired; ordinary sessions stay live.
    t.host.now+=5000;assert(management(retire).status==200);
    assert(state().at("connected")==false && healthy->request("status","GET").status==200);
    assert(t.mutations==0 && t.wtp.io==0);
  }
#endif
}

struct PeerClock : pi::WtpScheduleClock {
  backend_test::Peer peer;
  std::uint64_t now_ms() const override {return peer.now;}
  std::optional<std::uint64_t> utc_now_ns() const override {return backend_test::Peer::utc_base+peer.mono();}
  void wait_ms(std::uint64_t n) override {peer.advance(peer.now+n);}
};
void runtime_recovery_admission_cases() {
#ifdef WSPRRY_PICO_RECOVERY
  for (unsigned phase=0;phase<7;++phase) {
    // Failed/unknown output is deliberately latched by the production host.
    // Isolate each fixture instead of bypassing that latch for test teardown.
    std::cout.flush();
    const auto child=fork();assert(child>=0);
    if (child) {
      int result=0;assert(waitpid(child,&result,0)==child);
      assert(WIFEXITED(result) && WEXITSTATUS(result)==0);
      continue;
    }
    PeerClock clock;clock.peer.device_id=device;
    WtpSettings settings;settings.device_id=device;
    set_wtp_runtime_for_test(settings,clock,clock.peer,{backend_test::sid,backend_test::owner_id,device},[&]{clock.peer.open();return true;});
    unsigned consumer_calls=0;
    set_wtp_management_transports_for_test({},[&](auto,auto,auto,auto){++consumer_calls;return pi::PicoHttpResponse{503,"",{}};});
    assert(wtp_runtime_inspect().ok);
    if (phase==0) wtp_runtime_prepare_skip();
    else if (phase==6) {clock.peer.close();clock.peer.advance(clock.peer.now+1000);wtp_runtime_poll_idle();}
    else {
      clock.peer.owner=std::string(32,'e');clock.peer.expiry=clock.peer.mono()+60'000'000'000ULL;
      if (phase>=2) {clock.peer.job=pi::wtp::Job{};clock.peer.job->job_id=backend_test::jid;clock.peer.job->total_duration_ns=60'000'000'000ULL;}
      const std::array states{pi::wtp::State::Empty,pi::wtp::State::Empty,pi::wtp::State::Loaded,pi::wtp::State::Armed,pi::wtp::State::Running,pi::wtp::State::Failed};
      clock.peer.state=states[phase];clock.peer.start=clock.peer.mono()+5'000'000'000ULL;
      clock.peer.active=phase==4 || phase==5;
      clock.peer.advance(clock.peer.now+1000);wtp_runtime_poll_idle();
    }
    assert(!wtp_runtime_switch_allowed());
    const auto before=Json::parse(wtp_runtime_json());const auto operations=clock.peer.operations;
    const auto owner=clock.peer.owner;const auto state=clock.peer.state;const bool output=clock.peer.active;
    const auto selection=Json::parse(wtp_runtime_management("member/management","GET","","").body).at("selection_id");
    const Json inspect={{"action","inspect_logins"},{"carrier","consumer_station"},{"host","192.168.1.53"}};
    const Json retire={{"action","retire_login"},{"carrier","consumer_station"},{"host","192.168.1.53"},{"password","test-password"},
        {"selection_id",selection},{"scope",{{"target_selector",std::string(32,'b')},{"device_id",device},{"boot_id",backend_test::boot},{"generation","1"}}}};
    for (const auto &request : {inspect,retire}) {
      const auto result=wtp_runtime_management("member/management","POST",request.dump(),"");
      assert(result.status==409 && Json::parse(result.body).at("error").at("code")=="host_busy_or_output_unresolved");
    }
    const auto after=Json::parse(wtp_runtime_json());
    for (const auto *field : {"phase","session_phase","uncertain","safety_fault","owns","recovery_required","remote"})
      assert(before.at(field)==after.at(field));
    assert(consumer_calls==0 && clock.peer.operations==operations && clock.peer.owner==owner && clock.peer.state==state && clock.peer.active==output);
    assert(!wtp_runtime_switch_allowed());
    // exit() runs static runtime cleanup while this typed clock/peer is still
    // alive; no fixture operation clears ownership or output uncertainty.
    std::exit(0);
  }
#endif
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
#ifdef WSPRRY_PICO_TYPE23
void type23_profile_cases() {
  for (bool ap : {false,true}) for (const auto &call : {"AA0NT/P","<AA0NT>","PJ4/AA0NT","AA0NT/09"}) {
    const std::string grid=std::string(call)=="AA0NT/P" ? "EM18" : "EM18IG";
    Test t(true,call,grid);
    if (ap) { t.authority="192.168.4.1";t.origin="http://192.168.4.1";t.interface=pico::network::ConsumerInterface::AccessPoint; }
    const auto carrier=ap ? "consumer_ap" : "consumer_station";
    auto c=t.client();assert(c->login(device,carrier,"test-password").status==200);
    const auto state=t.coordinated(*c);assert(state.status==200);
    const auto saved=Json::parse(state.body);
    assert(saved["config"]["station"]["callsign"]==call && saved["config"]["station"]["locator"]==grid);
    const auto before=t.f.flash.bytes;const auto profile=t.profiles.sequence();
    pi::PicoHttpResponse resources;
    assert(t.app->idle_management([&]{ resources=pi::pico_resource_request([&](auto p,auto m,auto b,auto e){return c->request(p,m,b,e);},
      c->device(),c->boot(),"application","GET",{}, {},carrier,c->generation());},true));
    assert(resources.status==200);
    const auto application=Json::parse(resources.body)["application"];
    assert(application["schema"]=="transmitter-application/2" && application["station"]["station"]["callsign"]==call && application["wspr_plan"]["supported"]==true);
    assert(application["wspr_plan"]["frames"]==(grid.size()==4 || std::string(call).starts_with('<') ? 1 : 2));
    assert(t.f.flash.bytes==before && t.profiles.sequence()==profile && t.wtp.io==0 && t.mutations==0);
    c->logout();
  }
  std::cout<<"Type 2/3 profile readback through native station/AP crypto and strict resource adapters passed\n";
}
#endif
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
    // Test-only second transport client: genuine shipped browser packets share
    // this exact Pico manager with the production native runtime session.
    if (command.contains("consumer_packet")) {
      const auto packet=command["consumer_packet"];
      std::vector<std::uint8_t> bytes;
      const std::string encoded=packet.at("body_b64");
      assert(encoded.empty()||pico::network::bootstrap_unb64url(encoded,bytes,0,32768));
      const auto response=t.send(packet.at("path"),packet.at("method"),std::string(bytes.begin(),bytes.end()),packet.at("media"));
      const auto view=std::span(reinterpret_cast<const std::uint8_t*>(response.body.data()),response.body.size());
      std::cout<<Json{{"status",response.status},{"media",response.media_type},{"body_b64",pico::network::bootstrap_b64url(view)}}.dump()<<std::endl;
      continue;
    }
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
  alias_fixed_vector(vectors);
#ifdef WSPRRY_PICO_RECOVERY
  std::ifstream recovery_vector_file(std::filesystem::path(argv[1]).parent_path()/"Consumer-Recovery-v1-vectors.json");
  assert(recovery_vector_file.is_open());
  Json recovery_vectors;recovery_vector_file >> recovery_vectors;
  pi::consumer_radio_fixed_vectors(recovery_vectors);
  alias_fixed_vector(recovery_vectors);
  std::cout << "Consumer-Recovery/1 fixed AEAD/HKDF and native selector vectors passed\n";
#endif
  retirement_job_authority_cases();
  local_retirement_refusal_cases();
  runtime_recovery_continuity_cases();
  runtime_recovery_admission_cases();
  recovery_cases();
  cases(false);
  cases(true);
  resource_admission_cases();
#ifdef WSPRRY_PICO_TYPE23
  type23_profile_cases();
#endif
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
        bad["schema"] = "transmitter-management/3";
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
    for (const auto version : {1,2}) {
      auto supported=good;
      supported["schema"]="transmitter-management/"+std::to_string(version);
      supported["limits"]["consumer_sessions"]=version==1?1:3;
      supported["limits"].erase("consumer_pending_logins");
      if(version==2)supported["limits"]["consumer_pending_logins"]=1;
      assert(pi::valid_management_contract(supported,"consumer_station",device,t.f.service.status().boot_id,"1"));
      for(const Json& invalid : {Json(-1),Json(0),Json(true),Json(1.5),Json(4),Json("3"),Json(nullptr)}) {
        auto bad=supported;bad["limits"]["consumer_sessions"]=invalid;
        assert(!pi::valid_management_contract(bad,"consumer_station",device,t.f.service.status().boot_id,"1"));
        if(version==2){bad=supported;bad["limits"]["consumer_pending_logins"]=invalid;
          assert(!pi::valid_management_contract(bad,"consumer_station",device,t.f.service.status().boot_id,"1"));}
      }
    }
    assert(t.mutations == 0);
  }
  // Discovery refusals pass through the actual native coordinator and never write.
  for (unsigned variant=0;variant<12;++variant) {
    Test t;auto c=t.client();assert(c->login(device,"consumer_station","test-password").status==200);
    const auto exchange=[&](const std::string& resource,const std::string& method,const std::string& body,const std::string& revision) {
      auto response=c->request(resource,method,body,revision);
      if(resource=="management") {
        auto j=Json::parse(response.body);
        if(variant==0)j["schema"]="transmitter-management/3";
        if(variant==1)j["limits"]["consumer_sessions"]=0;
        if(variant==2)j["limits"]["consumer_sessions"]=true;
        if(variant==3)j["limits"]["consumer_sessions"]=3.0;
        if(variant==4)j["limits"]["consumer_sessions"]=-1;
        if(variant==5)j["limits"]["consumer_sessions"]=100;
        if(variant==6)j["limits"]["consumer_pending_logins"]=2;
        if(variant==7)j["target"]["boot_id"]=std::string(32,'b');
        if(variant==8)j.erase("schema");
        if(variant==9)j=Json::array();
        if(variant==10)j=nullptr;
        if(variant==11){
          const bool old=j["schema"]=="transmitter-management/1";
          j["schema"]=old?"transmitter-management/2":"transmitter-management/1";
          j["limits"]["consumer_sessions"]=old?3:1;
          j["limits"].erase("consumer_pending_logins");
          if(old)j["limits"]["consumer_pending_logins"]=1;
        }
        response.body=j.dump();
      }
      return response;
    };
    pi::PicoHttpResponse config,resource;
    assert(t.app->idle_management([&]{
      config=pi::pico_configuration_request(exchange,c->device(),c->boot(),"GET",{}, {},"consumer_station",c->generation());
      resource=pi::pico_resource_request(exchange,c->device(),c->boot(),"application","GET",{}, {},"consumer_station",c->generation());
    },true));
    assert(config.status==409 && resource.status==409 && t.mutations==0 && c->active());
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
