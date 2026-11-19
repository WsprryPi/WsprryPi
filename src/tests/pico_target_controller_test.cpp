// SPDX-License-Identifier: MIT
#include "wtp_integration/pico_target_controller.hpp"
#include "wtp_integration/control_contract.hpp"
#include "wtp_settings_json.hpp"
#include <atomic>
#include <barrier>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace wsprrypi;
using Json=nlohmann::json;
#define CHECK(x) do{if(!(x))throw std::runtime_error("line "+std::to_string(__LINE__)+": " #x);}while(false)
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
  return {{"contract","Wsprry-Control/1"},{"schema","management"},{"target",target},{"profile","engineering"},{"preclock",true},{"profile_generation",nullptr},{"selected","engineering_mtls"},{"carriers",carriers},{"unsupported",{"plain_wtp_json","tls_wtp_json","ble_json"}},
    {"operations",Json::parse(control_management_operations())},
    {"limits",{{"http_body",32768},{"consumer_plaintext",32724},{"config",4096},{"update",2048},{"login_ms",30000},{"idle_ms",1800000},{"absolute_ms",28800000},{"consumer_sessions",3},{"consumer_pending_logins",1}}}};
}


WtpSettings settings(char id) {
    WtpSettings value;value.transport="network";value.hostname="wsprrypico-"+std::string(32,id)+".local";
    value.device_id=std::string(32,id);value.tcp_port=443;value.tls_ca="/private/fixture/ca";
    value.tls_certificate="/private/fixture/cert";value.tls_key="/private/fixture/key";return value;
}
}
int main() {
    std::string temporary;
    try {
        WtpBrowserApi host_api([]{return Json::object();},[](const auto &,const auto &,const auto &,const auto &,const auto &){return PicoHttpResponse{404,"{}",{}};},[](const auto &){return PicoHttpResponse{404,"{}",{}};});
        const auto host_caps=Json::parse(host_api.handle({"GET","/api/control/v1/capabilities",{},{},false,{}}).body);
        CHECK(!host_caps.contains("member_resources")&&!host_caps.contains("management_carriers")&&!host_caps.contains("management_scope"));
        CHECK(!host_caps["features"].contains("member_configuration")&&!host_caps["features"].contains("member_resources")&&!host_caps["features"].contains("network"));
        char pattern[]="/tmp/pico-controller-test-XXXXXX";const auto directory=mkdtemp(pattern);CHECK(directory);temporary=directory;
        WtpCatalog catalog(temporary+"/catalog.json");
        std::string a,b;
        for(char id:{'a','b'}) {
            const auto [before,revision]=catalog.snapshot({});(void)before;
            const auto [after,next]=catalog.mutate({{"operation","add"},{"name",std::string("Pico ")+id},{"method","manual"},
                {"discovery_id",""},{"settings",management_profile_settings_json(settings(id))},{"consent_plain",false}},revision,{});(void)next;
            (id=='a'?a:b)=after["profiles"].back()["id"];
        }
        std::atomic<unsigned> reads{0},writes{0};
        PicoTargetController controller(catalog,[]{return std::optional<WtpSettings>{};},temporary+"/management",std::string(32,'c'),
            [&](const auto &profile){return PicoManagementTarget::Fixtures{[&,selected=profile.settings.device_id](const auto &resource,const auto &method,const auto &,const auto &) {
                if(method!="GET")++writes;else ++reads;
                if(resource=="management") {auto value=management();value["target"]["device_id"]=selected;return PicoHttpResponse{200,value.dump(),{}};}
                if(resource=="capabilities") {auto caps=canonical_caps();caps["contract"]="Wsprry-Control/1";return PicoHttpResponse{200,caps.dump(),{}};}
                return PicoHttpResponse{404,R"({"error":{"code":"not_found"}})",{}};
            },{},{}};});
        const auto path=[](char id){return "/api/control/v1/targets/"+std::string(32,id)+"/pico/management";};
        const BrowserRequest getA{"GET",path('a'),{}, {},false},getB{"GET",path('b'),{}, {},false};
        auto first=controller.handle(getA,a,{},{}),second=controller.handle(getB,b,{},{});
        CHECK(first.response.status==200&&second.response.status==200&&first.context!=second.context&&reads==0&&writes==0);
        const auto connect=[&](char id,const std::string &profile,const std::string &context) {
            return controller.handle({"POST",path(id),R"({"contract":"Wsprry-Control/1","action":"engineering_mtls"})",{},true},profile,context,{});
        };
        CHECK(connect('a',a,first.context).response.status==200);
        CHECK(connect('b',b,second.context).response.status==200);
        const auto targets=controller.handle({"GET","/api/control/v1/targets",{},{},false},{},{},{});
        CHECK(Json::parse(targets.response.body)["targets"].size()==2&&writes==0);
        // Different targets can share the controller lookup map without one
        // spuriously becoming a recovery failure. Native operations remain
        // serialized independently for each target.
        std::barrier rendezvous(2);std::atomic_bool concurrent_failure{false};
        const auto inspect=[&](const BrowserRequest &request,const std::string &profile,const std::string &context){
            for(unsigned index=0;index<128;++index){
                rendezvous.arrive_and_wait();
                const auto result=controller.handle(request,profile,context,{});
                if(result.response.status!=200||result.context!=context)concurrent_failure=true;
            }
        };
        std::thread inspectA(inspect,std::cref(getA),std::cref(a),std::cref(first.context));
        std::thread inspectB(inspect,std::cref(getB),std::cref(b),std::cref(second.context));
        inspectA.join();inspectB.join();CHECK(!concurrent_failure&&writes==0);
        const auto count=reads.load();
        CHECK(controller.handle(getA,b,first.context,{}).response.status!=200);
        CHECK(controller.handle(getA,a,second.context,{}).response.status==409);
        CHECK(reads==count&&writes==0);
        // A profile rename is an explicit catalog mutation, never an alias for
        // continuing an old target connection under the edited profile.
        {std::unique_lock gate(pico_management_catalog_gate());const auto [snapshot,revision]=catalog.snapshot({});(void)snapshot;
            const auto changed=catalog.mutate({{"operation","rename"},{"id",a},{"name","Temporary name"}},revision,{});
            catalog.mutate({{"operation","rename"},{"id",a},{"name","Pico a"}},changed.second,{});}
        CHECK(controller.handle(getA,{},first.context,{}).response.status==409);
        CHECK(reads==count&&writes==0);
        CHECK(controller.handle(getB,b,second.context,{}).response.status==200);
        const auto disconnected=controller.handle({"POST",path('b'),R"({"contract":"Wsprry-Control/1","action":"disconnect"})",{},true},b,second.context,{});
        CHECK(disconnected.response.status==200&&disconnected.context.empty());
        CHECK(controller.handle(getB,{},second.context,{}).response.status==409);
        const auto replacement=controller.handle(getB,b,{},{});
        CHECK(replacement.response.status==200&&replacement.context!=second.context);
        CHECK(writes==0);
        std::filesystem::remove_all(temporary);
        std::cout<<"PASS: independent enrolled profiles, explicit management authentication, cross-context/profile refusal, catalog fencing, and fresh reconnect handles without execution writes\n";
    } catch(const std::exception &e){if(!temporary.empty())std::filesystem::remove_all(temporary);std::cerr<<e.what()<<'\n';return 1;}
}
