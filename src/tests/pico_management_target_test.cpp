// SPDX-License-Identifier: MIT
// Transport foundation tests; these do not qualify the target HTTP API or journals.
#include "wtp_integration/pico_management_target.hpp"
#include "wtp_integration/management_resolver.hpp"
#include "wtp_integration/control_contract.hpp"
#include "pico_network_diagnostics_fixture.hpp"
#include <chrono>
#include <future>
#include <iostream>
using namespace wsprrypi;
using Json=nlohmann::json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x); } while(false)
namespace {
const std::string device(32,'a'), boot(32,'1');
const Json target{{"scope","member"},{"device_id",device},{"boot_id",boot}};
Json canonical_caps() {
  Json result{{"execution",{{"modes",{"wspr","tone"}},{"engine","test"},
    {"frequency_ranges",Json::array({{{"minimum_nhz","100000000000000"},{"maximum_nhz","30000000000000000"}}})},
    {"max_events",512},{"max_job_duration_ns","3600000000000"},
    {"minimum_arm_lead_ns","1000000"},{"maximum_arm_ahead_ns","300000000000"},{"maximum_arm_uncertainty_ns","500000000"},
    {"maximum_holdover_age_ns","3600000000000"},{"output_disable_timeout_ns","50000000"}}},
    {"features",Json::object()},{"active_execution_connections",false},{"max_network_connections",2},
    {"max_pending_connections",1},{"max_handshakes",1},{"max_body_bytes",32768}};
  for(const auto name:{"config","schedules","network","application","station","hardware","consumer_setup_identity","softap","ble","restart"})result["features"][name]=std::string(name)!="consumer_setup_identity";
  for(const auto name:{"application_resources","configuration_updates","management_carriers","saved_progress","gpio_drive","amplifier_timing","si5351","gps","gray_line","schedule_stop"})result[name]=Json::parse(control_descriptor(name));
  return result;
}
Json management() {
  Json carriers=Json::array();for(const auto name:{"engineering_mtls","engineering_field","consumer_station","consumer_ap","usb_physical"}) carriers.push_back({{"carrier",name},{"built_in",true},{"available",true}});
  return {{"contract","Wsprry-Control/1"},{"schema","management"},{"target",target},{"profile","engineering"},{"preclock",true},{"profile_generation",nullptr},{"selected","engineering_mtls"},{"carriers",carriers},
    {"operations",Json::parse(control_management_operations())},
    {"limits",{{"http_body",32768},{"consumer_plaintext",32724},{"config",4096},{"update",2048},{"login_ms",30000},{"idle_ms",1800000},{"absolute_ms",28800000},{"consumer_sessions",3},{"consumer_pending_logins",1}}}};
}

struct Resolver final : TlsResolver {
  std::vector<std::string> addresses;
  bool cancelled{};
  explicit Resolver(std::vector<std::string> values):addresses(std::move(values)){}
  bool begin(const std::string &,unsigned) override {return true;}
  auto poll() -> std::optional<std::vector<std::string>> override {return addresses;}
  void cancel() noexcept override {cancelled=true;}
};
WtpSettings profile(char id='a') {
  WtpSettings s;s.transport="network";
  s.hostname="wsprrypico-"+std::string(32,id)+".local";s.device_id=std::string(32,id);
  s.tcp_port=443;s.tls_ca="/not/read/by/typed/fixture/ca.crt";
  s.tls_certificate="/not/read/by/typed/fixture/client.crt";s.tls_key="/not/read/by/typed/fixture/client.key";
  return s;
}
struct Member {
  Json management_value=management(),caps=canonical_caps();
  unsigned mutations{},reads{};
  Json network=Json::parse(network_diagnostics_fixture(true));
  bool corrupt_network_write{};
  bool reboot_during_network{};
  bool wrong_network_requested{};
  bool native_stopped{},wrong_stop_target{},lose_stop{};
  unsigned stop_revision{1};
  std::function<void()> on_read;
  explicit Member(char id='a') {management_value["target"]["device_id"]=std::string(32,id);caps["contract"]="Wsprry-Control/1";}
  PicoHttpResponse exchange(const std::string &resource,const std::string &method,const std::string &body,const std::string &) {
    if(method!="GET")++mutations;else ++reads;
    if(on_read){auto callback=std::exchange(on_read,{});callback();}
    if(resource=="management")return {200,management_value.dump(),{}};
    if(resource=="capabilities")return {200,caps.dump(),{}};
    if(resource=="schedules/stop") {
      if(method=="POST") {
        native_stopped=true;++stop_revision;
        if(lose_stop)return {503,R"({"error":{"code":"transport_unavailable"}})",{}};
      }
      auto bound=target;if(wrong_stop_target)bound["device_id"]=std::string(32,'b');
      return {200,Json{{"contract","Wsprry-Control/1"},{"schema","schedule-stop"},{"target",bound},
        {"wspr_enabled",!native_stopped},{"message_enabled",false},{"owned",!native_stopped},
        {"output_active",!native_stopped},{"stopped",native_stopped}}.dump(),"\"stop-"+std::to_string(stop_revision)+"\""};
    }
    if(resource=="network") {
      if(reboot_during_network)management_value["target"]["boot_id"]=std::string(32,'9');
      auto value=network;value["contract"]="Wsprry-Control/1";
      if(method=="PUT") {
        value["requested_enabled"]=Json::parse(body)["enabled"];value["withdrawal_pending"]=true;
        if(corrupt_network_write)value["unexpected"]=true;
        if(wrong_network_requested)value["requested_enabled"]=true;
      }
      return {200,value.dump(),"\"network-1\""};
    }
    return {404,R"({"error":{"code":"not_found"}})",{}};
  }
  PicoManagementTarget::Fixtures fixture() {
    return {[this](const auto &r,const auto &m,const auto &b,const auto &v){return exchange(r,m,b,v);},{},{}};
  }
};
void retired_management_operations() {
  for(unsigned fault=0;fault<4;++fault) {
    Member peer;
    PicoManagementTarget selected(profile(),peer.fixture());
    CHECK(selected.connect("engineering_mtls").status==200);
    auto &operations=peer.management_value["operations"];
    if(fault==0)operations["POST"].push_back("jobs");
    if(fault==1)operations["GET"].erase(operations["GET"].begin());
    if(fault==2)operations["GET"][1]=operations["GET"][0];
    if(fault==3)operations["GET"][0]="unknown-resource";
    CHECK(selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})","\"network-1\"").status==409);
    CHECK(peer.mutations==0);
  }
}
void addresses() {
  for(const auto *address:{"10.0.0.2","172.16.0.1","172.31.255.254","192.168.4.1","fd00::1","fc00::1"})
    CHECK(private_management_address(address,{}));
  for(const auto *address:{"127.0.0.1","0.0.0.0","169.254.169.254","8.8.8.8","172.32.0.1","172.15.0.1","192.169.0.1","224.0.0.1","255.255.255.255","::1","fe80::1","ff02::1","::ffff:192.168.1.1","2001:db8::1","10.0.0.2%1","10.0.0.2\r\nHost:other"})
    CHECK(!private_management_address(address,{}));
  CHECK(!private_management_address("10.0.0.2",{"10.0.0.2"}));
  CHECK(!private_management_address("fd00::1",{"fd00:0:0:0:0:0:0:1"}));
  for(const auto &answers:std::vector<std::vector<std::string>>{{"10.0.0.2","192.168.1.4"},{"10.0.0.2","127.0.0.1"},{"10.0.0.2","192.168.1.3"},{}}) {
    auto resolver=management_tls_resolver_for_test(std::make_unique<Resolver>(answers),[]{return std::optional<std::vector<std::string>>{{"192.168.1.3"}};});
    CHECK(resolver->begin("saved-host.local",443));
    const auto result=resolver->poll();CHECK(result.has_value());
    CHECK(*result==(answers.size()==2 && answers[1]=="192.168.1.4" ? std::vector<std::string>{"10.0.0.2"}:std::vector<std::string>{}));
  }
  auto resolver=management_tls_resolver_for_test(std::make_unique<Resolver>(std::vector<std::string>{"10.0.0.2"}),[]{return std::optional<std::vector<std::string>>{};});
  CHECK(resolver->poll()->empty());
}
void contexts() {
  Member a,b('b');PicoManagementTarget first(profile(),a.fixture()),second(profile('b'),b.fixture());
  CHECK(first.connect("engineering_mtls").status==200);
  {
    const auto reads=a.reads,mutations=a.mutations;unsigned admitted=0;
    const auto refusal=first.request_guarded("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})","\"network-1\"",
        [&]()->std::optional<PiPairMutationGuard>{++admitted;return {};});
    CHECK(refusal.status==409&&a.reads==reads&&a.mutations==mutations&&admitted==0);
    CHECK(first.request_guarded("network","GET","","",{}).status==409&&a.reads==reads);
  }
  CHECK(second.connect("engineering_mtls").status==200);
  CHECK(first.binding()["device_id"]==device && second.binding()["device_id"]==std::string(32,'b'));
  CHECK(first.connect("engineering_mtls").status==409);
  // A boot change invalidates this context before any write reaches a producer.
  a.management_value["target"]["boot_id"]=std::string(32,'2');
  CHECK(first.request("station","PUT","{}","\"r1\"").status==409);
  CHECK(first.binding()["connected"]==false && second.binding()["connected"]==true);
  CHECK(a.mutations==0 && b.mutations==0);
  Member changed;PicoManagementTarget stale(profile(),changed.fixture());
  CHECK(stale.connect("engineering_mtls").status==200);
  changed.caps["max_body_bytes"]=16384;
  CHECK(stale.request("station","PUT","{}","\"r1\"").status==409);
  CHECK(changed.mutations==0 && stale.binding()["connected"]==false);
  CHECK(second.request("unknown","GET").status==404);
  second.cancel();CHECK(second.request("station","PUT","{}","\"r1\"").status==409);
  CHECK(a.mutations==0 && b.mutations==0);
  for(unsigned fault=0;fault<7;++fault) {
    Member peer;
    if(fault==0)peer.management_value["extra"]=true;
    if(fault==1)peer.management_value["target"]["device_id"]=std::string(32,'b');
    if(fault==2)peer.management_value["contract"]="Wsprry-Control/2";
    if(fault==3)peer.caps["extra"]=true;
    if(fault==4)peer.caps["management_carriers"]["path"]="/api/control/v2/management";
    if(fault==5)peer.caps["application_resources"]["update"]="/arbitrary";
    if(fault==6)peer.caps["configuration_updates"]["max_update_bytes"]=65536;
    PicoManagementTarget refused(profile(),peer.fixture());
    const auto response=refused.connect("engineering_mtls");
    if(response.status!=409)throw std::runtime_error("Negotiation fault "+std::to_string(fault)+" accepted: "+response.body);
    CHECK(refused.request("station","PUT","{}","\"r1\"").status==409);
    CHECK(peer.mutations==0);
  }
  Member peer;PicoManagementTarget busy(profile(),peer.fixture());
  CHECK(busy.connect("engineering_mtls").status==200);
  std::promise<void> entered,release;auto released=release.get_future().share();
  peer.on_read=[&]{entered.set_value();released.wait();};
  auto request=std::async(std::launch::async,[&]{return busy.request("unknown","GET");});
  entered.get_future().wait();
  const auto response=busy.request("station","PUT","{}","\"r1\"");
  release.set_value();CHECK(response.status==409);const auto result=request.get();
  if(result.status!=404)throw std::runtime_error("In-flight read failed: "+result.body);
  CHECK(peer.mutations==0);
}
void native_stop() {
  Member peer;PicoManagementTarget selected(profile(),peer.fixture());
  CHECK(selected.connect("engineering_mtls").status==200);
  const auto read=selected.request("schedules/stop","GET");CHECK(read.status==200&&peer.mutations==0);
  const Json binding{{"device_id",device},{"boot_id",boot},{"profile_generation","1"},{"connection_generation","1"}};
  CHECK(pico_management_response_valid("schedules/stop","GET",read,binding));
  Json body{{"contract","Wsprry-Control/1"},{"schema","schedule-stop"},{"target",target},{"stop",true}};
  CHECK(selected.request("schedules/stop","POST",body.dump(),"\"stale\"").status==412&&peer.mutations==0);
  body["job_id"]=std::string(32,'c');
  CHECK(selected.request("schedules/stop","POST",body.dump(),read.etag).status==400&&peer.mutations==0);body.erase("job_id");
  body["target"]["boot_id"]=std::string(32,'9');
  CHECK(selected.request("schedules/stop","POST",body.dump(),read.etag).status==409&&peer.mutations==0);body["target"]=target;
  const auto stopped=selected.request("schedules/stop","POST",body.dump(),read.etag);
  CHECK(stopped.status==200&&peer.native_stopped&&peer.mutations==1);
  CHECK(pico_management_response_valid("schedules/stop","POST",stopped,binding,body.dump()));
  auto malformed=stopped;auto value=Json::parse(malformed.body);value["owned"]=true;malformed.body=value.dump();
  CHECK(!pico_management_response_valid("schedules/stop","POST",malformed,binding,body.dump()));
  Member lost;lost.lose_stop=true;PicoManagementTarget uncertain(profile(),lost.fixture());
  CHECK(uncertain.connect("engineering_mtls").status==200);
  CHECK(uncertain.request("schedules/stop","POST",body.dump(),"\"stop-1\"").status==503&&lost.mutations==1);
  CHECK(Json::parse(uncertain.request("schedules/stop","GET").body)["stopped"]==true&&lost.mutations==1);
  Member changed;changed.wrong_stop_target=true;PicoManagementTarget wrong(profile(),changed.fixture());
  CHECK(wrong.connect("engineering_mtls").status==200);
  CHECK(wrong.request("schedules/stop","POST",body.dump(),"\"stop-1\"").status==502&&changed.mutations==0);
}
void network_parity() {
  Member unsupported;unsupported.caps["features"]["network"]=false;
  PicoManagementTarget unavailable(profile(),unsupported.fixture());
  CHECK(unavailable.connect("engineering_mtls").status==200);
  CHECK(unavailable.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})","\"network-1\"").status==409);
  CHECK(unsupported.mutations==0);
  Member peer;PicoManagementTarget selected(profile(),peer.fixture());
  CHECK(selected.connect("engineering_mtls").status==200);
  const auto read=selected.request("network","GET");CHECK(read.status==200&&peer.mutations==0);
  const Json bound{{"device_id",device},{"boot_id",boot},{"profile_generation","1"},{"connection_generation","1"}};
  CHECK(pico_management_response_valid("network","GET",read,bound));
  auto malformed=read;auto extra=Json::parse(read.body);extra["extra"]=true;malformed.body=extra.dump();
  CHECK(!pico_management_response_valid("network","GET",malformed,bound));
  Json wifi{{"contract","Wsprry-Control/1"},{"schema","wifi"},{"target",target},{"generation","1"},{"pending_restart",false},{"storage_healthy",true},
      {"wifi",{{"ssid","Fixture"},{"password",nullptr},{"time_server","pool.ntp.org"}}}};
  const auto wifi_reply=[&]{return PicoHttpResponse{200,wifi.dump(),"\"wifi-1\""};};
  CHECK(pico_management_response_valid("wifi","PUT",wifi_reply(),bound));
  wifi["extra"]=true;CHECK(!pico_management_response_valid("wifi","PUT",wifi_reply(),bound));wifi.erase("extra");
  wifi["wifi"]["password"]="must-not-enter-a-replay-record";CHECK(!pico_management_response_valid("wifi","PUT",wifi_reply(),bound));wifi["wifi"]["password"]=nullptr;
  wifi["target"]["boot_id"]=std::string(32,'2');CHECK(!pico_management_response_valid("wifi","PUT",wifi_reply(),bound));
  const Json udp_test{{"contract","Wsprry-Control/1"},{"schema","udp-logging-test"},{"target",target},{"accepted",true},{"receipt","unknown"}};
  CHECK(pico_management_response_valid("udp-logging/test","POST",{200,udp_test.dump(),{}},bound));
  auto udp_bad=udp_test;udp_bad["target"]["device_id"]=std::string(32,'b');
  CHECK(!pico_management_response_valid("udp-logging/test","POST",{200,udp_bad.dump(),{}},bound));

  CHECK(selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})","\"stale\"").status==412);
  CHECK(selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":0})",read.etag).status==400);
  CHECK(selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false,"extra":0})",read.etag).status==400);
  CHECK(peer.mutations==0);
  for(const auto &field:{"enabled","withdrawal_pending","ntp_pool","requested_enabled"}) {
    const auto original=peer.network[field];peer.network.erase(field);
    CHECK(selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})",read.etag).status==502);
    peer.network[field]=original;CHECK(peer.mutations==0);
  }
  peer.network["extra"]=true;CHECK(selected.request("network","GET").status==502);peer.network.erase("extra");
  const auto changed=selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})",read.etag);
  CHECK(changed.status==200&&peer.mutations==1);
  const auto result=Json::parse(changed.body);
  CHECK(result["enabled"]==true&&result["requested_enabled"]==false&&result["withdrawal_pending"]==true);
  peer.corrupt_network_write=true;
  const auto unknown=selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})",read.etag);
  CHECK(unknown.status==503&&Json::parse(unknown.body)["error"]["code"]=="management_outcome_unknown");
  CHECK(peer.mutations==2);
  peer.corrupt_network_write=false;peer.wrong_network_requested=true;
  CHECK(selected.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})",read.etag).status==503);
  CHECK(peer.mutations==3);
  Member rebooting;PicoManagementTarget stale(profile(),rebooting.fixture());
  CHECK(stale.connect("engineering_mtls").status==200);rebooting.reboot_during_network=true;
  CHECK(stale.request("network","PUT",R"({"contract":"Wsprry-Control/1","enabled":false})",read.etag).status==409);
  CHECK(rebooting.mutations==0&&stale.binding()["connected"]==false);
}
}
int main() {try {addresses();retired_management_operations();contexts();native_stop();network_parity();std::cout<<"PASS: bounded private resolver, independent management negotiation, native stop/refusal/uncertain readback, identity/boot/capability fencing, network parity, and concurrency refusal\n";}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
