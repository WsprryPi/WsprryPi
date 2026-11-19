// SPDX-License-Identifier: MIT
// Three isolated device owners and actual protected HTTP/PAKE/TLS. No RF,
// output execution, USB, LAN discovery, installation or service operations.
// Native configuration uses explicit simulated-only policy and inhibited jobs.
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "wtp_integration/pi_management_service.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include "wtp_integration/pi_pair_local_service.hpp"
#include "wtp_integration/pi_pair_guarded_http.hpp"
#include "wtp_integration/pi_pair_machine_service.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/browser_http.hpp"
#include "web_server_routes.hpp"
#include "backend_http_guard.hpp"
#include "privileged_network_runtime.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
#include <fstream>
#include <atomic>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
std::string id(char c){return std::string(32,c);}
void require(bool value){if(!value)throw std::runtime_error("pairing_browser_fixture_failed");}
std::shared_ptr<PiPairHistory> provision(const std::string &directory,char identity){
    std::filesystem::create_directory(directory);std::shared_ptr<PiPairHistory> result;
    ControllerRetirementBook book(directory+"/proof",directory+"/assignments.json",id(identity),Json::array(),{},
        [&](PiPairInitializationProof proof){result=PiPairHistory::initialize(directory+"/pairing",std::move(proof));});
    require(bool(result));return result;
}
class Clock final:public wsprrypico::wtp::Clock {public:wsprrypico::wtp::ClockSnapshot snapshot() const override{
    using namespace wsprrypico::wtp;return {ClockState::Synchronized,1000000000000ULL,10000000000ULL,1000,0,LeapState::Normal,std::nullopt};}};
class Identity final:public wsprrypico::wtp::IdentitySource {public:std::string new_boot_id() override{return id('f');}};
struct Device {
    PiPairAdmission admission;ManagementBrowserSessions sessions;PiPairDiscovery discovery;
    std::shared_ptr<PiPairHistory> history;std::shared_ptr<PiPairLocalService> service;
    std::unique_ptr<WtpCatalog> catalog;std::string self;
    PiPairGuardedHttpServer http;std::thread web;unsigned port{};
    const SupportRequestGuardSnapshot guard{true,"fixture.local",{},{{"127.0.0.1","255.0.0.0"}}};
    Device(const std::string &directory,char identity):discovery(id(identity)),history(provision(directory,identity)),http(admission){
        self=id(identity);catalog=std::make_unique<WtpCatalog>(directory+"/catalog.json");
        const auto selected=http.bind_to_any_port("127.0.0.1");require(selected>0);port=selected;
    }
    void start(){
        require(service&&service->begin());
        http.Post("/api/control/v1/host/management-session",[this](const auto &request,auto &response){
            const auto origin=request.has_header("Origin")?std::optional(request.get_header_value("Origin")):std::nullopt;
            const bool allowed=evaluate_backend_http_request(request.method,request.path,request.remote_addr,request.get_header_value("Host"),origin,guard)==BackendHttpGuardDecision::allowed;
            if(!allowed||!sessions.establish(request,response)){response.status=403;response.set_content(controller_target_error(403,"csrf_failed").body,"application/json");return;}
            response.set_content(R"({"contract":"Wsprry-Control/1","schema":"controller-browser-session"})","application/json");
        });
        web_server_routes::register_pairing(http,[this]{return service;},[this]{return guard;});
        http.Get("/api/control/v1/host/readiness",[this](const auto &,auto &response){response.set_content(Json{{"contract","Wsprry-Control/1"},{"schema","host-readiness"},{"device_id",self},{"boot_id",id('d')},{"role","controller-only"},{"state","ready"},{"recovery_editable",false}}.dump(),"application/json");});
        const auto remote=[this](const auto &request,auto &response){
            const auto reply=service->remote(request,guard);
            if(!reply){response.status=404;response.set_content(controller_target_error(404,"unsupported_resource").body,"application/json");return;}
            response.status=reply->payload.status;response.set_header("Cache-Control","no-store");
            if(!reply->payload.etag.empty())response.set_header("ETag",reply->payload.etag);
            if(!reply->location.empty())response.set_header("Location",reply->location);
            if(!reply->recovery_resource.empty())response.set_header("X-WsprryPi-Recovery-Resource",reply->recovery_resource);
            if(!reply->readback_revision.empty())response.set_header("X-WsprryPi-Readback-Revision",reply->readback_revision);
            response.set_content(reply->payload.body,"application/json");
        };
        http.Get("/api/control/v1/targets(?:/.*)?",remote);http.Put("/api/control/v1/targets/.*",remote);http.Post("/api/control/v1/targets/.*",remote);
        web=std::thread([this]{http.listen_after_bind();});http.wait_until_ready();require(service->transport_ready());
    }
    void stop(){http.stop();if(web.joinable())web.join();if(service){service->request_stop();service->stop_and_drain();service.reset();}}
    ~Device(){try{stop();}catch(...){}}
};
}
int main(){try{
    initialize_privileged_network_runtime("enforced");char pattern[]="/tmp/pi-pair-browser-XXXXXX";const auto directory=mkdtemp(pattern);require(directory);
    struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{directory};
    const auto ini=std::string(directory)+"/fixture.ini";
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";resolve_backend_specific_config(config);config_to_json();
    {std::ofstream file(ini);file<<"[Security]\nPrivileged Network Safety = enforced\n";}require(!chmod(ini.c_str(),0600));
    iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
    patch_all_from_web({{"WSPR",{{"Grid Square","EM18"},{"Call Sign","AA0NT"}}}});
    WtpCatalog native_catalog(ini+".wtp-devices.json");PiManagedStore native_store(ini,id('a'),native_catalog.pi_management_anchor(id('a'),{}));native_store.recover();native_store.initialize(jConfig);
    set_config_persistence_owner([&](const auto &internal,const auto &writer){native_store.persist(internal,writer);},[&](const auto &internal){native_store.validate_snapshot(internal);},
        [&](const auto &internal,const auto &image){native_store.persist_stock(internal,image);});
    iniFile.set_file_writer([&](const auto &path,const auto &image){require(path==ini);native_store.write_committed_ini(image);});
    struct Clear{~Clear(){iniFile.set_file_writer({});set_config_persistence_owner({},{},{});}} clear;
    Clock clock;Identity identity;wsprrypico::wtp::InhibitedRfEngine engine;wsprrypico::wtp::JobService jobs(clock,engine,identity);WtpPiAuthority authority(jobs,false);
    PiLocalManagementService native(native_store,id('a'),"Fixture target Pi",[&]()->std::optional<PiManagementObservation>{
        const auto state=authority.management_snapshot(id('a'));
        return PiManagementObservation{id('a'),state.first,state.second,[&]{return authority.begin_configuration();},[&]{authority.end_configuration();},{},{},{}};
    });
    Device target(std::string(directory)+"/target",'a');
    target.service=std::make_shared<PiPairLocalService>(target.history,target.sessions,target.admission,target.discovery,id('a'),id('c'));
    PiPairMachineService machine(*target.history,target.sessions,id('a'),
        [&](const auto &,const auto &){return PicoHttpResponse{200,Json{{"contract","Wsprry-Control/1"},{"schema","targets"},{"targets",Json::array({native.target()})}}.dump(),{}};},
        [&](const auto &principal,const auto &request,const auto &){
            const auto parsed=wtp_browser_http_request(request);require(parsed.has_value());PiPairHttpReply reply;
            reply.payload=native.handle(*parsed,principal,*target.history,&reply.readback_revision);return reply;
        });
    PiPairServer tls(target.admission,[&](const auto &identity){return target.history->management_credential(identity);},
        [&](const auto &principal){return target.history->accept_finished(principal,&target.sessions)!=PiPairHistory::ManagementAccess::Denied;},
        [&](const auto &principal,const auto &request,auto &connection,const auto &cancelled){return machine.handle(principal,request,connection,cancelled);},PiPairServer::Access::LoopbackTest);
    target.start();require(tls.start());
    const auto start_controller=[&](Device &device,char identity){
        device.discovery.available(true);
        require(device.discovery.resolved({1,"Fixture target Pi"},"local","wsprry-management-"+id('a')+".local.",31418,pi_pair_discovery_txt(id('a'),id('c'),target.port)));
        const PiPairLocalService::LoopbackTest endpoints{{{"127.0.0.1",target.port},{"127.0.0.1",tls.port()}}};
        auto remote=std::make_shared<PiPairRemoteController>([&device]{return device.history;},device.admission,id(identity),*device.catalog,
            std::string(directory)+"/remote-"+identity,PiPairRemoteController::LoopbackTest{{"127.0.0.1",tls.port()}});
        device.service=std::make_shared<PiPairLocalService>(device.history,device.sessions,device.admission,device.discovery,id(identity),id('d'),endpoints,remote);device.start();
    };
    Device controller(std::string(directory)+"/controller",'b');start_controller(controller,'b');
    Device replacement(std::string(directory)+"/replacement",'e');start_controller(replacement,'e');
    std::cout<<Json{{"target_port",target.port},{"controller_port",controller.port},{"replacement_port",replacement.port}}.dump()<<std::endl;
    std::string command;while(std::getline(std::cin,command)){
        if(command=="STOP_OLD"){controller.stop();std::cout<<"ACK"<<std::endl;}
        else if(command=="ADVANCE_NATIVE"){native.advance_migration();std::cout<<"ACK"<<std::endl;}
        else if(command=="QUIT")break;
        else throw std::runtime_error("pairing_browser_fixture_command");
    }
    replacement.stop();controller.stop();target.service->request_stop();tls.stop();target.stop();
    return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
