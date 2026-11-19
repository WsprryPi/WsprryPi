#include "wtp_integration/pi_pair_contract.hpp"
#include "wtp_integration/pi_pair_retirement_client.hpp"
#include "wtp_integration/pi_pair_machine_backend.hpp"
#include "wtp_integration/fleet_test.hpp"
#include "wtp_integration/fleet_store.hpp"
#include "wtp_integration/management_identity.hpp"
// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler_serialization.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include "scheduling_internal.hpp"
#include "privileged_network_runtime.hpp"
#include "wtp_integration/catalog.hpp"
#include "wtp_integration/controller_contract.hpp"
#include "wtp_integration/pi_management_service.hpp"
#include "wtp_integration/pi_pair_server.hpp"
#include "wtp_integration/pi_pair_machine_service.hpp"
#include "wtp_integration/pi_pair_remote_controller.hpp"
#include "wtp_integration/pi_pair_local_service.hpp"
#include "wtp_integration/pico_management_journal.hpp"
#include "wtp_integration/pi_managed_records.hpp"
#include "recurrence_authority.hpp"
#include "wtp_integration/controller_retirement.hpp"
#include "wtp_settings_json.hpp"
#include "wtp_integration/pi_pair_client.hpp"
#include "wtp_integration/pi_pair_history.hpp"
#include "wtp_integration/pi_pair_operations.hpp"
#include "wtp_integration/pi_pair_peers.hpp"
#include "wtp_integration/pi_pair_records.hpp"
#include "wtp_integration/pi_pair_renewal.hpp"
#include "wtp_integration/pi_pair_maintenance.hpp"
#include "wtp_integration/local_browser_principal.hpp"
#include "wtp_integration/management_browser_session.hpp"
#include "wtp_integration/browser_http.hpp"
#include "WTP-Server/include/wtp_server/inhibited_rf_engine.hpp"
#include <atomic>
#include <future>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <sys/stat.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;std::atomic_uint checks{};
void check(bool value,const std::source_location at=std::source_location::current()){
    const auto count=++checks;if(!value)throw std::runtime_error("machine service check "+std::to_string(count)+" at "+std::to_string(at.line()));}
std::string id(char c){return std::string(32,c);}
PiPairSecret key(){std::array<unsigned char,32> value{};value.fill(7);return PiPairSecret(value);}
std::unique_ptr<PiPairHistory> history(const std::string &directory,bool controller=false){
    const auto self=id(controller?'b':'a');auto result=std::make_unique<PiPairHistory>(directory,self);ManagementBrowserSessions sessions;
    httplib::Request request;request.method="POST";request.path="/api/control/v1/host/pairing/window";request.target=request.path;request.remote_addr="192.168.1.20";
    request.headers={{"Host","device.local"},{"Origin","http://device.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
    request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-window"},{"operation_id",id('e')},{"action","recover"},{"expected_authorization_generation","0"}}.dump();
    httplib::Response response;check(sessions.establish(request,response));const auto cookie=response.get_header_value("Set-Cookie");
    request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
    const auto principal=sessions.authorize_local_mutation(request,{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}});check(principal.has_value());
    result->recover_access(*wtp_browser_http_request(request),*principal,sessions);const auto before=result->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,self),peers=parse_pi_pair_peers(before.peers->bytes,self);
    Json pair{{"schema","pi-management-pair/1"},{"self_id",self},{"peer_id",id(controller?'a':'b')},{"pair_id",id('c')},
        {"authorization_generation","1"},{"role","manager"},{"state","active"},{"pending",nullptr},{"previous",nullptr},
        {"current",{{"epoch","1"},{"tls_psk",key().storage_encoding()},{"service_ms","0"},{"connections","0"}}},{"receipt",nullptr},{"retirement_verifier",key().storage_encoding()}};
    if(controller)peers["profiles"].push_back({{"profile_id",id('9')},{"profile_generation","1"},{"target_id",id('a')},
        {"endpoint",{{"host","target.local"},{"port",31418}}},{"pair",pair},{"stage","active"},{"initial_operation_id",id('8')},{"initial_request_digest",std::string(64,'e')}});
    else {catalog["authorization_generation"]="1";catalog["active"]=pair;}
    auto operations=parse_pi_pair_operations(before.operations.bytes,self);
    if(!controller)operations["entries"].push_back({{"scope","paired"},{"principal_id",id('b')},{"target_id",self},{"pair_id",id('c')},
        {"authorization_generation","1"},{"operation_id",id('c')},{"request_digest",std::string(64,'e')},{"state","committed"},
        {"result_digest",std::string(64,'f')},{"unresolved",false},{"receipt",nullptr},{"continuation",nullptr}});
    result->publish(before,catalog,operations,peers);result->begin_service();return result;
}
class Clock final:public wsprrypico::wtp::Clock {public:wsprrypico::wtp::ClockSnapshot snapshot() const override{
    using namespace wsprrypico::wtp;return {ClockState::Synchronized,1000000000000ULL,10000000000ULL,1000,0,LeapState::Normal,std::nullopt};}};
class Identity final:public wsprrypico::wtp::IdentitySource {public:std::string new_boot_id() override{return id('f');}};

}
int recover_registration(const std::string &root,unsigned port){
    initialize_privileged_network_runtime("enforced");PiPairAdmission outgoing;ManagementBrowserSessions sessions;
    auto history=std::make_shared<PiPairHistory>(root+"/controller",id('b'));check(history->available()&&history->unclean_start()&&history->begin_service());
    check(history->outgoing_renewal_due(id('9')));
    PiPairRenewal renewal(*history,sessions,outgoing,id('b'),PiPairRenewal::LoopbackTest{{"127.0.0.1",port}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(renewal.advance(id('9'))!=PiPairRenewal::Result::Ready&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(150));
    check(!history->outgoing_renewal_due(id('9')));
    WtpCatalog catalog(root+"/remote-controller-devices.json");
    PiPairRemoteController owner([&]{return history;},outgoing,id('b'),catalog,root+"/remote-management",PiPairRemoteController::LoopbackTest{{"127.0.0.1",port}});
    unsigned source_reads=0;owner.authority_source([&]{++source_reads;return controller_target_error(503,"storage_unavailable");});
    owner.receipt_eviction_observer([&]{sessions.retire();});check(owner.observe(id('9')));
    bool pending=true;
    while(pending&&std::chrono::steady_clock::now()<deadline){
        std::this_thread::sleep_for(std::chrono::milliseconds(150));(void)owner.maintain(id('9'));pending=false;
        const auto operations=parse_pi_pair_operations(history->snapshot().operations.bytes,id('b'));
        for(const auto &entry:operations["entries"])if(!entry["continuation"].is_null()&&entry["continuation"]["kind"]=="authority-registration-intent")pending=true;
    }
    check(!pending&&source_reads==0);history->finish_service();
    std::cout<<"PASS fresh-process authority registration: retained unknown reply, mandatory unclean renewal, exact receipt-only recovery, unavailable live source book, no registration resend\n";
    return 0;
}
int recover_handover(const std::string &root,unsigned port){
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    initialize_privileged_network_runtime("enforced");
    PiPairHistory keys(root+"/handover-recovery-pairing",id('a'));check(keys.available()&&keys.unclean_start()&&keys.begin_service());
    const auto ini=root+"/remote-handover-target.ini";WtpCatalog catalog(ini+".wtp-devices.json");
    PiManagedStore store(ini,id('a'),catalog.pi_management_anchor(id('a'),{}));store.recover();
    check(store.remote_handover()->at("requests")[0]["state"]=="awaiting_receipt");
    Clock clock;Identity identity;wsprrypico::wtp::InhibitedRfEngine engine;wsprrypico::wtp::JobService jobs(clock,engine,identity);WtpPiAuthority authority(jobs,false);
    PiLocalManagementService native(store,id('a'),"Handover target",[&]()->std::optional<PiManagementObservation>{auto state=authority.management_snapshot(id('a'));
        return PiManagementObservation{id('a'),state.first,state.second,[&]{return authority.begin_configuration();},[&]{authority.end_configuration();},{},{},{}};});
    PiPairAdmission admission;PiPairRetirementClient client(keys,admission,PiPairRetirementClient::LoopbackTest{{"127.0.0.1",port}});
    native.advance_remote_handover(client,[]{return false;});check(store.remote_handover()->at("requests")[0]["state"]=="confirmed");
    native.advance_migration();check(!store.migration_pending()&&store.recurrence_authority()["state"]=="staged"&&store.settings()["scheduling"]["enabled"]==false);
    const auto receipt=store.receipt(id('c'),"1",id('6'));check(receipt&&(*receipt)["state"]=="committed");keys.finish_service();
    std::cout<<"PASS fresh-process target handover: exact retained unknown command, original persisted keys, unclean start, status-only verified retirement, disabled atomic staging and exact receipt\n";
    return 0;
}
int resume_target_crash(const std::string &root,const std::string &mode,unsigned point){
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    initialize_privileged_network_runtime("enforced");config.transmit_backend=TransmitBackendKind::SIMULATED;config.simulated_backend_override=true;
    const auto name=root+"/resume-crash-"+mode+"-"+std::to_string(point),ini=root+"/remote-handover-target.ini";
    auto keys=std::make_shared<PiPairHistory>(name+"-target",id('a'));check(keys->available()&&keys->begin_service());
    WtpCatalog catalog(ini+".wtp-devices.json");PiManagedStore store(ini,id('a'),catalog.pi_management_anchor(id('a'),{}),
        [&](PiManagedWritePoint at){if(static_cast<unsigned>(at)==point)_exit(73);});store.recover();
    Clock clock;Identity identity;wsprrypico::wtp::InhibitedRfEngine engine;wsprrypico::wtp::JobService jobs(clock,engine,identity);WtpPiAuthority authority(jobs,false);
    PiLocalManagementService native(store,id('a'),"Crash target",[&]()->std::optional<PiManagementObservation>{auto observed=authority.management_snapshot(id('a'));
        PiManagementObservation result{id('a'),observed.first,observed.second,[&]{return authority.begin_configuration();},[&]{authority.end_configuration();},{},{},{}};
        result.wtp_binding=PiWtpBinding{observed.first.revocation_generation,31417,31415};return result;});
    if(mode=="grant"){
        const auto context=Json::parse(native.handle({"GET","/api/control/v1/targets/"+id('a')+"/context",{},{},false},true).body);
        const auto controller=store.recurrence_authority()["controllers"][0];
        const Json body{{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",context["binding"]},
            {"capability_revision",context["capability_revision"]},{"operation_id",id('2')},{"expected_revision",store.settings()["revision"]},
            {"expected_authority_generation",store.recurrence_authority()["generation"]},{"resume_controller_id",controller["controller_id"]},{"resume_assignment_id",controller["assignment_id"]}};
        ManagementBrowserSessions sessions;httplib::Request request;request.method="POST";request.path="/api/control/v1/targets/"+id('a')+"/migration/rollback";
        request.target=request.path;request.remote_addr="192.168.1.20";request.body=body.dump();request.headers={{"Host","device.local"},{"Origin","http://device.local"},
            {"Content-Type","application/json"},{"X-WsprryPico-Request","1"},{"If-Match","\""+body["expected_revision"].get<std::string>()+"\""}};
        httplib::Response response;check(sessions.establish(request,response));const auto cookie=response.get_header_value("Set-Cookie");
        request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
        const auto principal=sessions.authorize_local_mutation(request,{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}});check(bool(principal));
        const auto browser=wtp_browser_http_request(request);check(bool(browser));(void)native.handle(*browser,*principal,*keys);return 4;
    }
    check(mode=="ack");auto source=std::make_shared<PiPairHistory>(name+"-source",id('b'));check(source->available()&&source->begin_service());
    PiPairAdmission incoming,outgoing;ManagementBrowserSessions sessions,source_sessions;
    PiPairMachineService machine(*keys,sessions,id('a'),[](const auto &,const auto &){return controller_target_error(503,"authority_pending");},
        [&](const auto &principal,const auto &request,const auto &){const auto browser=wtp_browser_http_request(request);check(bool(browser));return PiPairHttpReply{native.handle(*browser,principal,*keys),{},{}};});
    PiPairServer server(incoming,[&](const auto &identity){return keys->management_credential(identity);},
        [&](const auto &principal){return keys->accept_finished(principal,&sessions)!=PiPairHistory::ManagementAccess::Denied;},
        [&](const auto &principal,const auto &request,auto &connection,const auto &cancelled){return machine.handle(principal,request,connection,cancelled);},PiPairServer::Access::LoopbackTest);
    check(server.start());PiPairRenewal renewal(*source,source_sessions,outgoing,id('b'),PiPairRenewal::LoopbackTest{{"127.0.0.1",server.port()}});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(renewal.advance(id('9'))!=PiPairRenewal::Result::Ready&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(!source->outgoing_renewal_due(id('9')));
    const auto peers=parse_pi_pair_peers(source->snapshot().peers->bytes,id('b'));const auto &pair=peers["profiles"][0]["pair"];
    const PiPairTlsIdentity tls{PiPairTlsPurpose::Management,pair["pair_id"].get<std::string>(),pair["authorization_generation"].get<std::string>(),pair["current"]["epoch"].get<std::string>()};
    std::ifstream file(root+"/resume-crash-ack.json");const auto ack=Json::parse(file);PiPairClient client(outgoing,PiPairClient::Access::LoopbackTest);
    (void)client.exchange({"127.0.0.1",server.port()},id('a'),[&](const auto &identity){return source->outgoing_credential(id('9'),identity);},tls,
        [&](const auto &principal){return source->record_outgoing_finished(id('9'),principal);},"POST","/api/pair/v1/resume/ack",ack.dump());
    return 5;
}
int main(int argc,char **argv){try{
    if(argc==5&&std::string_view(argv[1])=="--resume-target-crash")return resume_target_crash(argv[2],argv[3],static_cast<unsigned>(std::stoul(argv[4])));
    if(argc==4&&std::string_view(argv[1])=="--recover-handover")return recover_handover(argv[2],static_cast<unsigned>(std::stoul(argv[3])));
    if(argc==4&&std::string_view(argv[1])=="--recover-authority-registration")return recover_registration(argv[2],static_cast<unsigned>(std::stoul(argv[3])));
    const bool refuse_second=argc==2&&std::string_view(argv[1])=="--refuse-second";check(argc==1||refuse_second);
    char pattern[]="/tmp/pi-pair-machine-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary);const std::string root=temporary,ini=root+"/settings.ini";
    struct Remove {std::string root;~Remove(){std::filesystem::remove_all(root);}} remove{root};
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";resolve_backend_specific_config(config);config_to_json();
    {std::ofstream file(ini);file<<"[Security]\nPrivileged Network Safety = enforced\n";}check(!chmod(ini.c_str(),0600));
    iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
    patch_all_from_web({{"WSPR",{{"Grid Square","EM18"},{"Call Sign","AA0NT"}}}});initialize_privileged_network_runtime("enforced");
    WtpCatalog catalog(ini+".wtp-devices.json");PiManagedStore store(ini,id('a'),catalog.pi_management_anchor(id('a'),{}));store.recover();store.initialize(jConfig);
    set_config_persistence_owner([&](const auto &internal,const auto &writer){store.persist(internal,writer);},[&](const auto &internal){store.validate_snapshot(internal);},
        [&](const auto &internal,const auto &image){store.persist_stock(internal,image);});
    iniFile.set_file_writer([&](const auto &path,const auto &image){check(path==ini);store.write_committed_ini(image);});
    struct Clear {~Clear(){iniFile.set_file_writer({});set_config_persistence_owner({},{},{});}} clear;
    auto paired=history(root+"/pairing");std::shared_ptr<PiPairHistory> controller=history(root+"/controller",true);Clock clock;Identity identity;wsprrypico::wtp::InhibitedRfEngine engine;
    wsprrypico::wtp::JobService jobs(clock,engine,identity);WtpPiAuthority authority(jobs,false);bool turn_off=false,revoke=false;
    ManagementBrowserSessions removal_sessions;Json removal_receipt;
    PiLocalManagementService service(store,id('a'),"This Pi",[&]()->std::optional<PiManagementObservation>{
        auto state=authority.management_snapshot(id('a'));
        return PiManagementObservation{id('a'),state.first,state.second,[&]{
            if(turn_off){turn_off=false;set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);}
            if(revoke){
                revoke=false;httplib::Request request;request.method="POST";request.path="/api/control/v1/host/pairing/remove";request.target=request.path;request.remote_addr="192.168.1.20";
                request.headers={{"Host","device.local"},{"Origin","http://device.local"},{"Content-Type","application/json"},{"X-WsprryPico-Request","1"}};
                request.body=Json{{"contract","Wsprry-Control/1"},{"schema","pair-remove"},{"operation_id",id('5')},{"expected_authorization_generation","1"}}.dump();
                httplib::Response response;check(removal_sessions.establish(request,response));const auto cookie=response.get_header_value("Set-Cookie");
                request.headers.emplace("Cookie",cookie.substr(0,cookie.find(';')));request.headers.emplace("X-WsprryPi-CSRF",response.get_header_value("X-WsprryPi-CSRF"));
                const auto principal=removal_sessions.authorize_local_mutation(request,{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}});check(principal.has_value());
                const auto removed=paired->remove_access(*wtp_browser_http_request(request),*principal,removal_sessions,id('f'));
                check(removed.status==200&&!removal_sessions.owns(*principal));removal_receipt=Json::parse(removed.body);
                check(paired->remove_access(*wtp_browser_http_request(request),*principal,removal_sessions,id('f')).status==403);
            }
            return authority.begin_configuration();},[&]{authority.end_configuration();},
            [&,boot=state.first.remote.boot_id](const auto &expected){return authority.reserve_observed_stop(id('a'),boot,expected);},
            [&]{authority.release_observed_stop();},[&]{check(authority.local_stop());}};
    });
    PiPairAdmission incoming,outgoing;ManagementBrowserSessions sessions;PiPairTlsIdentity tls_identity{PiPairTlsPurpose::Management,id('c'),"1","1"};
    std::atomic_bool hold_settings{false},settings_entered{false},release_settings{false};
    bool lose_prepare=false,lose_commit=false,lose_native_reply=false,malform_native_reply=false,lose_registration_reply=false;unsigned prepares=0,commits=0,registration_requests=0;std::string rotation_operation,profile_generation="1";
    const Json unsigned_resume_ack={{"contract","Wsprry-Pair/1"},{"schema","resume-ack"},{"operation_id",id('1')},{"pair_id",id('c')},
        {"authorization_generation","1"},{"target_id",id('a')},{"controller_id",id('b')},{"assignment_id",id('4')},
        {"grant_digest",std::string(64,'6')},{"outcome","refused"},{"controller_generation","3"},{"receipt_digest",std::string(64,'8')}};
    const auto signed_resume_ack=controller->resume_acknowledgement(unsigned_resume_ack);check(bool(signed_resume_ack));
    // Independently calculated with Python's hmac/hashlib and canonical JSON.
    check((*signed_resume_ack)["proof"]=="K0wVwzKAlQwiO8zBrH79y3Q-2NW94l6-246bFww1QWM");
    std::optional<Json> resume_probe;bool resume_probe_expected=false;unsigned resume_probes=0;
    const auto make_machine=[&]{return std::make_unique<PiPairMachineService>(*paired,sessions,id('a'),
        [&](const auto &,const auto &){return PicoHttpResponse{200,Json{{"contract","Wsprry-Control/1"},{"schema","targets"},{"targets",Json::array({service.target()})}}.dump(),{}};},
        [&](const auto &principal,const httplib::Request &request,const auto &){
            const auto browser=wtp_browser_http_request(request);check(browser.has_value());if(request.method!="GET")check(!browser->mutation_context);
            PiPairHttpReply reply;reply.payload=service.handle(*browser,principal,*paired,&reply.readback_revision);return reply;
        });};
    auto machine=make_machine();
    PiPairServer server(incoming,[&](const auto &requested){return paired->management_credential(requested);},
        [&](const auto &principal){return paired->accept_finished(principal,&sessions)!=PiPairHistory::ManagementAccess::Denied;},
        [&](const auto &principal,const httplib::Request &request,auto &connection,const auto &cancelled){
            if(resume_probe){
                const auto proof=paired->resume_outcome_proof(principal,*resume_probe);check(bool(proof)==resume_probe_expected);++resume_probes;
                if(proof){check(!controller->admit_resume_outcome(*proof));const auto guard=paired->admit_resume_outcome(*proof);check(bool(guard));}
            }
            if(request.method=="GET"&&request.path.ends_with("/settings")&&hold_settings.exchange(false)){
                settings_entered.store(true);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while(!release_settings.load()&&!cancelled()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            auto response=machine->handle(principal,request,connection,cancelled);
            if(request.method=="POST"&&request.path=="/api/pair/v1/authorities/register"){
                ++registration_requests;
                if(lose_registration_reply&&response.payload.status==200){lose_registration_reply=false;throw std::runtime_error("fixture lost registration reply");}
            }
            if(lose_native_reply&&request.method=="PUT"&&request.path.ends_with("/settings")&&response.payload.status==200){lose_native_reply=false;throw std::runtime_error("fixture lost native save reply");}
            if(malform_native_reply&&request.method=="PUT"&&request.path.ends_with("/settings")&&response.payload.status==200){
                malform_native_reply=false;auto forged=Json::parse(response.payload.body);forged["request_digest"]=std::string(64,'e');response.payload.body=forged.dump();
            }
            if(request.path.starts_with("/api/pair/v1/rotate/")&&response.payload.status==200){
                const auto body=Json::parse(request.body);
                if(rotation_operation.empty())rotation_operation=body["operation_id"].get<std::string>();
                check(body["operation_id"]==rotation_operation);
                if(request.path.ends_with("prepare")){++prepares;if(lose_prepare){lose_prepare=false;throw std::runtime_error("fixture lost prepare reply");}}
                else {++commits;if(lose_commit){lose_commit=false;throw std::runtime_error("fixture lost commit reply");}}
            }
            return response;
        },PiPairServer::Access::LoopbackTest);check(server.start());
    WtpCatalog remote_catalog(root+"/remote-controller-devices.json");
    auto remote_journal=std::make_unique<PicoManagementJournal>(root+"/remote-management",id('b'),id('a'),remote_catalog.management_history_authority({}));
    {
        check(remote_catalog.remote_management({}).empty());
        PiPairRemoteController remote([&]{return controller;},outgoing,id('b'),remote_catalog,root+"/remote-management",PiPairRemoteController::LoopbackTest{{"127.0.0.1",server.port()}});
        check(Json::parse(remote.targets().body)["targets"].empty());
        check(!remote.observe(id('7'))&&remote.observe(id('9')));
        check(!remote.observe(id('9'))); // No observation poll faster than 1 Hz.
        const auto listed=remote.targets();check(listed.status==200&&Json::parse(listed.body)["targets"].size()==1);
        check(remote.resolve(id('a'))==id('9')&&remote.resolve(id('a'),id('9'))==id('9'));
        check(!remote.resolve(id('b'))&&!remote.resolve(id('d'))&&!remote.resolve(id('a'),id('8')));

        BrowserRequest request{"GET","/api/control/v1/targets/"+id('a')+"/context",{},{},false};
        const auto observed=remote.read(request,id('9'));check(observed.payload.status==200&&Json::parse(observed.payload.body)["authorization"]["pair_id"]==id('c'));
        check(remote_catalog.remote_management({})==Json{{id('a'),{{"parent_id",id('a')},{"profile_id",id('9')},{"profile_generation","1"},{"pair_id",id('c')},{"authorization_generation","1"},{"product","WsprryPi"}}}});
        check(!remote_catalog.snapshot({}).first.contains("remote_management"));
        check(remote_catalog.management_history_authority({}).read().devices.contains(id('a')));
        request.path="/api/control/v1/targets/"+id('a')+"/settings";
        const auto settings=remote.read(request,id('9'));check(settings.payload.status==200);
        hold_settings.store(true);settings_entered.store(false);release_settings.store(false);
        auto waiting=std::async(std::launch::async,[&]{return remote.read(request,id('9'));});
        const auto entered_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!settings_entered.load()&&std::chrono::steady_clock::now()<entered_deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const bool entered=settings_entered.load();
        const auto during=remote.targets();const auto same_target=remote.read(request,id('9'));
        const auto resolved=remote.resolve(id('a'));
        release_settings.store(true);const auto finished=waiting.get();
        check(entered&&during.status==200&&Json::parse(during.body)["targets"].size()==1&&resolved==id('9'));
        check(same_target.payload.status==409&&finished.payload.status==200);
        // A periodic inventory observation is not a context replacement. Keep
        // a valid editor/readback while its actual target identity is stable.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));check(remote.observe(id('9')));
        const auto saved=Json::parse(settings.payload.body),context=Json::parse(observed.payload.body);
        Json intent{{"contract","Wsprry-Control/1"},{"schema","target-save"},{"binding",context["binding"]},{"capability_revision",context["capability_revision"]},
            {"operation_id",id('a')},{"expected_revision",saved["revision"]},{"candidate",saved["saved"]}};
        intent["candidate"]["configuration"]["CW"]["Message"]="AUTHENTICATED REMOTE OWNER";
        BrowserRequest mutation{"PUT",request.path,intent.dump(),settings.payload.etag,true};
        auto unprotected=mutation;unprotected.mutation_context=false;check(remote.write(unprotected,id('9')).payload.status==403);
        const auto written=remote.write(mutation,id('9'));check(written.payload.status==200&&Json::parse(written.payload.body)["state"]=="committed");
        const auto after=store.settings();check(remote.write(mutation,id('9')).payload.body==written.payload.body&&store.settings()==after);
        auto changed=intent;changed["candidate"]["configuration"]["CW"]["Message"]="ID REUSE REFUSED";
        auto conflict=mutation;conflict.body=changed.dump();check(remote.write(conflict,id('9')).payload.status==409&&store.settings()==after);
        const auto malformed_settings=remote.read(request,id('9'));const auto malformed_saved=Json::parse(malformed_settings.payload.body);
        intent["operation_id"]=id('f');intent["expected_revision"]=malformed_saved["revision"];intent["candidate"]=malformed_saved["saved"];
        intent["candidate"]["configuration"]["CW"]["Message"]="MISMATCHED RECEIPT REFUSED";
        mutation.body=intent.dump();mutation.revision=malformed_settings.payload.etag;malform_native_reply=true;
        const auto malformed_result=remote.write(mutation,id('9'));
        check(malformed_result.payload.status==503&&!malform_native_reply&&remote_journal->receipt(id('f'))["state"]=="unknown");
        request.path="/api/control/v1/targets/"+id('a')+"/operations/"+id('f');
        check(remote.read(request,id('9')).payload.status==200&&remote_journal->receipt(id('f'))["state"]=="committed");
        request.path="/api/control/v1/targets/"+id('a')+"/settings";
        const auto next_settings=remote.read(request,id('9'));const auto next_saved=Json::parse(next_settings.payload.body);
        intent["operation_id"]=id('b');intent["expected_revision"]=next_saved["revision"];intent["candidate"]=next_saved["saved"];
        intent["candidate"]["configuration"]["CW"]["Message"]="LOST NATIVE OWNER REPLY";
        mutation.body=intent.dump();mutation.revision=next_settings.payload.etag;lose_native_reply=true;
        const auto lost=remote.write(mutation,id('9'));check(lost.payload.status==503&&!lose_native_reply&&store.receipt(id('c'),"1",id('b')));
        check(remote_journal->receipt(id('b'))["state"]=="unknown");
        // Stop owns a distinct exact operation even while the save's accepted
        // reply remains unknown. Actual TLS, native reservation and both WALs.
        BrowserRequest stop_context_request{"GET","/api/control/v1/targets/"+id('a')+"/context",{},{},false};
        const auto stop_context=Json::parse(remote.read(stop_context_request,id('9')).payload.body);
        stop_context_request.path="/api/control/v1/targets/"+id('a')+"/status";
        const auto stop_status=Json::parse(remote.read(stop_context_request,id('9')).payload.body);
        const Json stop_body{{"contract","Wsprry-Control/1"},{"schema","target-stop"},{"binding",stop_context["binding"]},
            {"capability_revision",stop_context["capability_revision"]},{"expected_revision",stop_status["revision"]},
            {"operation_id",id('0')},{"observed_output",stop_status["observed_output"]}};
        const auto unknown_save=remote_journal->receipt(id('b'));
        const auto independent_stop=remote.write({"POST","/api/control/v1/targets/"+id('a')+"/stop",stop_body.dump(),"\""+stop_status["revision"].get<std::string>()+"\"",true},id('9'));
        check(independent_stop.payload.status==200&&Json::parse(independent_stop.payload.body)["state"]=="committed");
        check(remote_journal->receipt(id('b'))==unknown_save&&remote_journal->receipt(id('0'))["state"]=="committed"&&store.settings()["configuration"]["Operation"]["Transmit"]==false);
        intent["operation_id"]=id('e');mutation.body=intent.dump();
        const auto barrier=remote.write(mutation,id('9'));check(barrier.payload.status==503&&barrier.location.ends_with(id('b'))&&barrier.recovery_resource=="settings");
        request.path="/api/control/v1/targets/"+id('a')+"/operations/"+id('b');
        const auto confirmed=remote.read(request,id('9'));check(confirmed.payload.status==200&&remote_journal->receipt(id('b'))["state"]=="committed");
        request.path="/api/control/v1/targets/"+id('a')+"/settings";
        const auto before_review=remote.read(request,id('9'));const auto before_review_body=Json::parse(before_review.payload.body);
        intent["operation_id"]=id('d');intent["expected_revision"]=before_review_body["revision"];intent["candidate"]=before_review_body["saved"];
        intent["candidate"]["configuration"]["CW"]["Message"]="EXPLICIT REVIEW AFTER LOST REPLY";
        mutation.body=intent.dump();mutation.revision=before_review.payload.etag;lose_native_reply=true;
        check(remote.write(mutation,id('9')).payload.status==503&&!lose_native_reply&&remote_journal->receipt(id('d'))["state"]=="unknown");
        const auto review_read=remote.read(request,id('9'));check(review_read.payload.status==200&&!review_read.readback_revision.empty());
        Json review{{"contract","Wsprry-Control/1"},{"schema","target-operation-review"},{"binding",Json::parse(review_read.payload.body)["binding"]},
            {"operation_id",id('d')},{"readback_revision",review_read.readback_revision}};
        BrowserRequest review_request{"POST","/api/control/v1/targets/"+id('a')+"/operations/"+id('d')+"/review",review.dump(),{},true};
        const auto unchanged=store.settings(),authority_before_review=store.recurrence_authority();
        const auto reviewed=remote.review(review_request,id('9'));check(reviewed.payload.status==200&&Json::parse(reviewed.payload.body)["schema"]=="target-operation-reviewed");
        check(store.settings()==unchanged&&store.recurrence_authority()==authority_before_review&&remote_journal->receipt(id('d'))["state"]=="unknown");
        const auto reviewed_history=remote_journal->snapshot();
        bool marked=false;for(const auto &record:reviewed_history["receipts"])if(record["receipt"]["operation_id"]==id('d'))marked=record["reconciled_revision"]==review_read.readback_revision&&record["response"].is_null();check(marked);
        intent["operation_id"]=id('e');mutation.body=intent.dump();check(remote.write(mutation,id('9')).payload.status==409);
        request.path="/api/control/v1/targets/"+id('a')+"/operations/"+id('d');
        check(Json::parse(remote.read(request,id('9')).payload.body)["state"]=="unknown");
        request.path="/api/control/v1/targets/"+id('d')+"/context";check(remote.read(request,id('9')).payload.status==404);
        request.path="/api/control/v1/targets/"+id('a')+"/pico/wifi";check(remote.read(request,id('9')).payload.status==404);
        request.path="/api/control/v1/targets/"+id('a')+"/settings";request.method="PUT";check(remote.read(request,id('9')).payload.status==400);
        set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);
        check(remote.targets().status==503&&remote.read(request,id('9')).payload.status==400);
        set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);
    }
    const PiPairTlsIdentity retirement_identity{PiPairTlsPurpose::Retirement,id('c'),"1",""};
    const auto retirement_keys=[&]{
        const auto sender=paired->retirement_client_credential(retirement_identity),receiver=controller->retirement_server_credential(retirement_identity);
        check(sender&&receiver&&sender->key.storage_encoding()==receiver->key.storage_encoding());
        check(sender->self_id==id('a')&&sender->peer_id==id('b')&&receiver->self_id==id('b')&&receiver->peer_id==id('a'));
        check(!paired->management_credential(retirement_identity)&&!controller->outgoing_credential(id('9'),retirement_identity));
        check(!paired->retirement_server_credential(retirement_identity)&&!controller->retirement_client_credential(retirement_identity));
        auto wrong=retirement_identity;wrong.epoch="1";check(!paired->retirement_client_credential(wrong)&&!controller->retirement_server_credential(wrong));
        wrong=retirement_identity;wrong.authorization_generation="2";check(!paired->retirement_client_credential(wrong)&&!controller->retirement_server_credential(wrong));
    };
    retirement_keys();
    {
        PiPairAdmission callbacks,requests;ManagementBrowserSessions callback_sessions;unsigned ordinary_calls=0;
        PiPairMachineService normal(*controller,callback_sessions,id('b'),[&](const auto &,const auto &){++ordinary_calls;return controller_target_error(503,"authority_pending");},
            [&](const auto &,const auto &,const auto &){++ordinary_calls;return PiPairHttpReply{controller_target_error(503,"authority_pending"),{},{}};});
        PiPairServer callback(callbacks,[&](const auto &requested){return controller->retirement_server_credential(requested);},
            [&](const auto &principal){return principal.self_id()==id('b')&&principal.peer_id()==id('a')&&bool(controller->retirement_server_credential(principal.identity()));},
            [&](const auto &principal,const auto &request,auto &connection,const auto &cancelled){return normal.handle(principal,request,connection,cancelled);},PiPairServer::Access::LoopbackTest);
        check(callback.start());PiPairClient target(requests,PiPairClient::Access::LoopbackTest);
        const auto refusal=target.exchange({"127.0.0.1",callback.port()},id('b'),[&](const auto &requested){return paired->retirement_client_credential(requested);},retirement_identity,
            [&](const auto &principal){return principal.self_id()==id('a')&&principal.peer_id()==id('b')&&bool(paired->retirement_client_credential(principal.identity()));},"GET","/api/control/v1/targets");
        check(refusal.principal&&refusal.reply&&refusal.reply->payload.status==403&&ordinary_calls==0);
        callback.stop();
        const Json unsigned_request={{"contract","Wsprry-Pair/1"},{"schema","retirement"},{"target_id",id('a')},{"controller_id",id('b')},
            {"assignment_id",id('4')},{"transition_id",id('5')},{"assignment_revision",std::string(64,'6')},
            {"retirement_generation","7"},{"pair_id",id('c')},{"authorization_generation","1"}};
        const auto signed_request=paired->retirement_request(unsigned_request);check(signed_request.has_value());
        auto forged=*signed_request;forged["retirement_generation"]="8";
        std::atomic_uint accepted{};Json callback_requests=Json::object(),callback_replies=Json::object();std::atomic_bool drop_callback{false},pending_callback{false},bad_pending{false};std::mutex callback_state;std::function<void(const Json &)> verify_target_handoff;bool actual_handover_source=false;
        const auto handoff_file=root+"/retirement-client-intent.json";
        PiPairServer verifier(callbacks,[&](const auto &identity){return controller->retirement_server_credential(identity);},
            [&](const auto &principal){return controller->retirement_finished(principal,true);},
            [&](const auto &principal,const auto &request,auto &,const auto &cancelled){
                const bool status=request.path.ends_with("/status");const Json body=parse_pi_pair_document(request.body,status?"retirement-status":"retirement");
                const auto authorized=controller->authorize_retirement(principal,body);
                if(!authorized)return PiPairHttpReply{controller_target_error(403,"unauthorized"),{},{}};
                if(actual_handover_source){
                    if(!status&&verify_target_handoff)verify_target_handoff(body);
                    auto response=wtp_fleet_retirement(*authorized,*controller,cancelled);
                    if(!status&&response.status==200){++accepted;if(drop_callback.exchange(false))throw std::runtime_error("Lost actual source retirement reply");}
                    return PiPairHttpReply{std::move(response),{},{}};
                }
                check(!controller->management_authorization(principal));std::lock_guard callback_lock(callback_state);const auto transition=body["transition_id"].get<std::string>();
                if(!status&&!callback_requests.contains(transition)){
                    if(verify_target_handoff)verify_target_handoff(body);
                    if(transition==id('9'))check(PiManagementJournal::read_ini_input(handoff_file).bytes==body.dump());
                    {const auto guard=controller->admit_retirement(*authorized);check(bool(guard));++accepted;}
                    const Json receipt={{"contract","Wsprry-Control/1"},{"schema","controller-retirement-operation"},{"controller_id",id('b')},
                        {"target_id",id('a')},{"assignment_id",id('4')},{"operation_id",id('8')},{"transition_id",transition},{"state","retired"},{"generation","3"}};
                    const auto response=controller->retirement_response(body,receipt);check(response.has_value());
                    callback_requests[transition]=body;callback_replies[transition]=*response;
                    if(drop_callback){drop_callback=false;throw std::runtime_error("Artificial lost accepted retirement reply");}
                }
                if(!callback_requests.contains(transition))return PiPairHttpReply{{404,Json{{"contract","Wsprry-Pair/1"},{"schema","result"},
                    {"pair_id",id('c')},{"state","rejected"},{"code","operation_unknown"},{"retryable",false}}.dump(),{}},{},{}};
                if(pending_callback)return PiPairHttpReply{{202,Json{{"contract","Wsprry-Pair/1"},{"schema","operation"},{"operation_id",transition},
                    {"pair_id",id('c')},{"authorization_generation","1"},{"state","pending"},{"result_digest",nullptr},
                    {"request_digest",bad_pending?std::string(64,'0'):PiManagementJournal::digest(canonical_pi_management_json(callback_requests[transition]))}}.dump(),{}},{},{}};
                return PiPairHttpReply{{200,callback_replies[transition].dump(),{}},{},{}};
            },PiPairServer::Access::LoopbackTest);
        check(verifier.start());
        const auto exchange=[&](const Json &body){return target.exchange({"127.0.0.1",verifier.port()},id('b'),
            [&](const auto &identity){return paired->retirement_client_credential(identity);},retirement_identity,
            [&](const auto &principal){return paired->retirement_finished(principal,false);},"POST","/api/pair/v1/retirement",body.dump());};
        const auto bad=exchange(forged);check(bad.principal&&bad.reply&&bad.reply->payload.status==403&&accepted==0);
        unsigned guarded_handoffs=0;
        const auto guarded_exchange=[&](const Json &body){return target.exchange({"127.0.0.1",verifier.port()},id('b'),
            [&](const auto &identity){return paired->retirement_client_credential(identity);},retirement_identity,
            [&](const auto &principal){return paired->retirement_finished(principal,false);},"POST","/api/pair/v1/retirement",body.dump(),{},[] {return false;},
            [&](const auto &principal){auto guard=paired->admit_retirement_request(principal,body);if(guard)++guarded_handoffs;return guard;});};
        const auto refused_handoff=guarded_exchange(forged);check(!refused_handoff.reply&&accepted==0&&guarded_handoffs==0);
        const auto good=guarded_exchange(*signed_request);check(good.principal&&good.reply&&good.reply->payload.status==200&&accepted==1&&guarded_handoffs>0);
        const auto response=parse_pi_pair_document(good.reply->payload.body,"retired");
        check(bool(paired->retired_proof(*good.principal,*signed_request,response)));
        check(!paired->resume_outcome_proof(*good.principal,*signed_resume_ack));
        auto changed=response;changed["receipt_digest"]=std::string(64,'0');check(!paired->retired_proof(*good.principal,*signed_request,changed));
        changed=*signed_request;changed["assignment_revision"]=std::string(64,'0');check(!paired->retired_proof(*good.principal,changed,response));
        check(!controller->retired_proof(*good.principal,*signed_request,response));
        {
            PiPairRetirementClient retired_client(*paired,requests,PiPairRetirementClient::LoopbackTest{{"127.0.0.1",verifier.port()}});
            const PiPairEndpoint registered{"wsprry-management-"+id('b')+".local.",31418};
            using Mode=PiPairRetirementClient::Mode;using State=PiPairRetirementClient::State;
            unsigned handoffs=0;PiManagementJournal handoff_journal(root+"/retirement-client-wal",id('a'),{{"recurrence",handoff_file}});
            const auto no_handoff=[&](const auto &){++handoffs;return false;};
            auto result=retired_client.exchange(registered,*signed_request,Mode::Status,no_handoff);
            check(result.state==State::Retired&&result.proof&&handoffs==0&&accepted==1);
            auto fresh=unsigned_request;fresh["transition_id"]=id('9');const auto frozen=paired->retirement_request(fresh);check(bool(frozen));
            result=retired_client.exchange(registered,*frozen,Mode::Request,no_handoff);check(result.state!=State::Retired&&accepted==1);
            const auto prepare=[&](const auto &){++handoffs;
                const auto saved=handoff_journal.inspect("recurrence");if(saved)return saved->bytes==frozen->dump();
                handoff_journal.prepare({{"recurrence",std::nullopt,frozen->dump()}});handoff_journal.commit();handoff_journal.recover();return true;};
            drop_callback=true;result=retired_client.exchange(registered,*frozen,Mode::Request,prepare);
            check(result.state==State::Unknown&&!result.proof&&accepted==2);
            const auto after_handoff=handoffs;const auto saved=handoff_journal.inspect("recurrence");check(saved&&saved->bytes==frozen->dump());
            result=retired_client.exchange(registered,*frozen,Mode::Status,no_handoff);
            check(result.state==State::Retired&&result.proof&&accepted==2&&handoffs==after_handoff&&handoff_journal.inspect("recurrence")->signature==saved->signature);
            pending_callback=true;result=retired_client.exchange(registered,*frozen,Mode::Status,no_handoff);
            check(result.state==State::Pending&&!result.proof&&handoffs==after_handoff&&accepted==2);
            bad_pending=true;result=retired_client.exchange(registered,*frozen,Mode::Status,no_handoff);check(result.state==State::Unknown&&!result.proof);
            pending_callback=false;bad_pending=false;fresh["transition_id"]=id('0');const auto absent=paired->retirement_request(fresh);check(bool(absent));
            result=retired_client.exchange(registered,*absent,Mode::Status,no_handoff);check(result.state==State::Unknown&&accepted==2&&handoffs==after_handoff);
            PiManagementJournal interrupted_handoff(root+"/retirement-client-interrupted-wal",id('a'),{{"recurrence",root+"/retirement-client-interrupted.json"}});
            result=retired_client.exchange(registered,*absent,Mode::Request,[&](const auto &)->bool{
                interrupted_handoff.prepare({{"recurrence",std::nullopt,absent->dump()}});interrupted_handoff.commit();
                throw std::runtime_error("Artificial failure after local dispatch decision");
            });
            check(result.state==State::Unknown&&accepted==2);interrupted_handoff.recover();
            check(interrupted_handoff.inspect("recurrence")->bytes==absent->dump());
            result=retired_client.exchange(registered,*absent,Mode::Status,no_handoff);check(result.state==State::Unknown&&accepted==2&&handoffs==after_handoff);
            auto wrong=registered;wrong.host="arbitrary.local";
            check(retired_client.exchange(wrong,*frozen,Mode::Request,prepare).state==State::Refused&&handoffs==after_handoff);
            wrong=registered;wrong.port=31417;check(retired_client.exchange(wrong,*frozen,Mode::Request,prepare).state==State::Refused);
        }
        {
            // A real first-installation target, actual normal TLS admission and
            // actual retirement TLS owner. The callback's positive idle proof
            // is a software fixture; no output/carrier is opened here.
            const auto source_ini=root+"/handover-real-source",source_directory=source_ini+".retirements";
            WtpSettings source_settings;source_settings.transport="network_plain";source_settings.hostname="old-target.local";source_settings.device_id=id('a');
            source_settings.tcp_port=31417;source_settings.allow_frequency_adjustment=true;
            const Json source_row={{"device_id",id('a')},{"name","Handover source"},{"settings",wtp_settings_json(source_settings)},
                {"product","WsprryPi"},{"revocation_generation","7"},{"management_port",31415},{"enabled",false},{"last_start_ns","0"},{"in_flight",false},
                {"schedule",{{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",60},{"phase_seconds",0}}}};
            {WtpFleetStore saved(source_ini+".wtp-assignments.json",id('b'));saved.update({},[&](Json &data){data["assignments"].push_back(source_row);});}
            {ControllerRetirementBook book(source_directory,source_ini+".wtp-assignments.json",id('b'),Json::array({source_row}));
                const auto before=book.read(id('d'),Json::array({source_row}));
                const Json intent={{"contract","Wsprry-Control/1"},{"schema","controller-retirement"},{"controller_id",id('b')},{"controller_incarnation",id('d')},
                    {"operation_id",id('1')},{"transition_id",id('2')},{"target_id",id('a')},{"assignment_id",before["assignments"][0]["assignment_id"]},
                    {"expected_assignment_revision",before["assignments"][0]["assignment_revision"]},{"expected_retirement_generation",before["generation"]}};
                book.retire(intent,before["revision"],id('d'),Json::array({source_row}));
                book.confirm_idle(id('a'),{{"device_id",id('a')},{"boot_id",id('f')},{"owner_id",nullptr},{"job_id",nullptr},{"output_active",false},
                    {"state","empty"},{"submitted_job_id",nullptr},{"session_id",nullptr},{"controller_owner_id",nullptr}});
            }
            WtpFleetTestHooks source_hooks;source_hooks.retirement_directory=source_directory;unsigned source_contacts=0;
            source_hooks.before_execution_contact=[&] {++source_contacts;throw std::runtime_error("Retired execution contact");};
            source_hooks.retirement_signer=[&](const Json &request,const Json &receipt){return controller->retirement_response(request,receipt);};
            start_wtp_fleet_for_test(source_ini,id('b'),source_hooks);struct StopHandoverSource{~StopHandoverSource(){stop_wtp_fleet();}} stop_handover_source;
            actual_handover_source=true;const auto source_before=Json::parse(wtp_fleet_api("GET",{},{},"retirements").body);
            const auto target_ini=root+"/remote-handover-target.ini";
            WtpCatalog target_catalog(target_ini+".wtp-devices.json");
            auto target_store=std::make_unique<PiManagedStore>(target_ini,id('a'),target_catalog.pi_management_anchor(id('a'),{}));
            target_store->recover();target_store->prepare_installation(false);check(target_store->installation_pending());
            auto internal=jConfig;internal["Meta"]["INI Filename"]=target_ini;internal["Operation"]["Transmit"]=false;internal["Operation"]["Enable on Boot"]="Never";
            Json setup;{std::ifstream input("wtp_integration/contract/control-controller-vectors.json");const auto vectors=Json::parse(input);
                for(const auto &entry:vectors["cases"])if(entry["name"]=="P209 complete recovery save")setup=entry["value"];}
            check(setup.is_object());setup["binding"]["device_id"]=id('a');setup["operation_id"]=id('d');setup["expected_revision"]=target_store->recovery_revision();
            setup["configuration"]=config_handler_serialization::saved_configuration_from_internal_json(internal);
            target_store->persist_recovery(internal,[]{},PiManagedStore::Operation{id('a'),"1","/api/control/v1/host/configuration-recovery",setup});
            bool ready_binding=false;unsigned binding_port=31417;
            const auto observe=[&]()->std::optional<PiManagementObservation>{auto state=authority.management_snapshot(id('a'));
                PiManagementObservation result{id('a'),state.first,state.second,[&]{return authority.begin_configuration();},[&]{authority.end_configuration();},{},{},{}};
                if(ready_binding)result.wtp_binding=PiWtpBinding{state.first.revocation_generation,binding_port,31415};
                return result;};
            auto native=std::make_unique<PiLocalManagementService>(*target_store,id('a'),"Handover target",observe);
            ManagementBrowserSessions target_sessions;PiPairAdmission target_admission;
            bool lose_resume_ack=false;
            PiPairMachineService native_machine(*paired,target_sessions,id('a'),[&](const auto &,const auto &){return PicoHttpResponse{200,
                Json{{"contract","Wsprry-Control/1"},{"schema","targets"},{"targets",Json::array({native->target()})}}.dump(),{}};},
                [&](const auto &principal,const auto &request,const auto &){const auto browser=wtp_browser_http_request(request);check(bool(browser));
                    auto response=native->handle(*browser,principal,*paired);
                    if(lose_resume_ack&&request.path=="/api/pair/v1/resume/ack"&&response.status==200){lose_resume_ack=false;throw std::runtime_error("fixture lost target ACK response");}
                    return PiPairHttpReply{std::move(response),{},{}};});
            PiPairServer native_server(target_admission,[&](const auto &identity){return paired->management_credential(identity);},
                [&](const auto &principal){return paired->accept_finished(principal,&target_sessions)!=PiPairHistory::ManagementAccess::Denied;},
                [&](const auto &principal,const auto &request,auto &connection,const auto &cancelled){return native_machine.handle(principal,request,connection,cancelled);},PiPairServer::Access::LoopbackTest);
            check(native_server.start());PiPairClient manager(requests,PiPairClient::Access::LoopbackTest);
            const auto native_request=[&](const std::string &method,const std::string &path,const Json &body=Json(nullptr)){
                httplib::Headers headers;if(body.is_object()&&body.contains("expected_revision"))headers.emplace("If-Match","\""+body["expected_revision"].get<std::string>()+"\"");
                return manager.exchange({"127.0.0.1",native_server.port()},id('a'),[&](const auto &identity){return controller->outgoing_credential(id('9'),identity);},tls_identity,
                    [&](const auto &principal){return controller->record_outgoing_finished(id('9'),principal);},method,path,body.is_null()?std::string{}:body.dump(),headers);};
            Json registration{{"contract","Wsprry-Pair/1"},{"schema","authority-register"},{"operation_id",id('a')},{"pair_id",id('c')},
                {"authorization_generation","1"},{"target_id",id('a')},{"controller_id",id('b')},{"controller_incarnation",source_before["controller_incarnation"]},{"controller_generation",source_before["generation"]},
                {"assignments",Json::array({{{"assignment_id",source_before["assignments"][0]["assignment_id"]},
                    {"assignment_revision",source_before["assignments"][0]["assignment_revision"]},{"state","retired"}}})},
                {"endpoint",{{"host","wsprry-management-"+id('b')+".local."},{"port",31418}}}};
            const auto registered=native_request("POST","/api/pair/v1/authorities/register",registration);check(registered.reply&&registered.reply->payload.status==200);
            check(target_store->recurrence_authority()["management"]["complete"]==true);
            const auto prefix="/api/control/v1/targets/"+id('a')+"/";
            const auto migration=native_request("GET",prefix+"migration");check(migration.reply&&migration.reply->payload.status==200);
            const auto view=Json::parse(migration.reply->payload.body);check(view["controllers"].size()==1&&view["controllers"][0]["controller_id"]==id('b'));
            const auto settings=target_store->settings();const auto ini_observation=PiManagementJournal::read_ini_input(target_ini);
            const auto context=native_request("GET",prefix+"context");check(context.reply&&context.reply->payload.status==200);
            const Json command={{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",view["binding"]},
                {"capability_revision",Json::parse(context.reply->payload.body)["capability_revision"]},{"operation_id",id('6')},
                {"expected_revision",settings["revision"]},{"expected_authority_generation",view["authority_generation"]},{"controllers",view["controllers"]}};
            const auto started=native_request("POST",prefix+"migration",command);
            if(!started.reply||started.reply->payload.status!=202)throw std::runtime_error("handover start: "+(started.reply?started.reply->payload.body:"no reply"));
            check(started.reply->payload.status==202);
            const auto receipt=Json::parse(started.reply->payload.body);check(receipt["state"]=="pending"&&target_store->migration_pending());
            check(target_store->remote_handover()->at("requests")[0]["state"]=="prepared");
            native->advance_migration();check(target_store->migration_pending()); // No proof, no staging.
            verify_target_handoff=[&](const Json &body){const auto frozen=target_store->remote_handover();check(bool(frozen));
                check((*frozen)["requests"][0]["state"]=="awaiting_receipt"&&(*frozen)["requests"][0]["request"]==body);};
            drop_callback=true;const auto accepted_before=accepted.load();
            PiPairRetirementClient retirement(*paired,requests,PiPairRetirementClient::LoopbackTest{{"127.0.0.1",verifier.port()}});
            native->advance_remote_handover(retirement,[]{return false;});
            check(accepted==accepted_before+1&&target_store->remote_handover()->at("requests")[0]["state"]=="awaiting_receipt");
            check(target_store->settings()["configuration"]==settings["configuration"]&&PiManagementJournal::read_ini_input(target_ini).signature==ini_observation.signature);
            native_server.stop();native.reset();target_store.reset();verify_target_handoff={};
            // Copy only isolated fixture-owned key history while no caller
            // mutates it. The child opens the exact persisted selector/images;
            // it does not seed replacement keys or share an active owner lock.
            const auto saved_keys=paired->snapshot();const auto namespace_id=Json::parse(saved_keys.authority->bytes)["namespace_id"].get<std::string>();
            std::filesystem::copy(root+"/pairing",root+"/handover-recovery-pairing",std::filesystem::copy_options::recursive);
            std::filesystem::copy(root+"/pairing.ns-"+namespace_id,root+"/handover-recovery-pairing.ns-"+namespace_id,std::filesystem::copy_options::recursive);
            const auto port=std::to_string(verifier.port());std::array<char *,5> arguments{argv[0],const_cast<char *>("--recover-handover"),const_cast<char *>(root.c_str()),const_cast<char *>(port.c_str()),nullptr};
            pid_t child{};check(posix_spawn(&child,argv[0],nullptr,nullptr,arguments.data(),environ)==0);int status{};
            check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&accepted==accepted_before+1);
            target_store=std::make_unique<PiManagedStore>(target_ini,id('a'),target_catalog.pi_management_anchor(id('a'),{}));target_store->recover();
            check(!target_store->migration_pending()&&target_store->recurrence_authority()["state"]=="staged");
            check(target_store->settings()["scheduling"]["enabled"]==false&&target_store->recurrence_authority()["retirements"].size()==1);
            const auto committed=target_store->receipt(id('c'),"1",id('6'));check(committed&&(*committed)["state"]=="committed"&&(*committed)["request_digest"]==receipt["request_digest"]);
            check(PiManagementJournal::read_ini_input(target_ini).signature==ini_observation.signature&&accepted==accepted_before+1);
            PiManagedStore cold(target_ini,id('a'),target_catalog.pi_management_anchor(id('a'),{}));cold.recover();
            check(cold.settings()==target_store->settings()&&cold.recurrence_authority()==target_store->recurrence_authority());
            // Reclamation uses this actual complete native owner, not an
            // empty catalog, age, pairing status or a caller-supplied boolean.
            {
                auto retention=history(root+"/closed-key-retention");const auto saved=retention->snapshot();
                auto catalog=parse_pi_pair_catalog(saved.catalog.bytes,id('a'));auto old=catalog["active"];
                old["state"]="revoked";old["current"]=nullptr;old["pending"]=nullptr;old["previous"]=nullptr;
                catalog["retired"].push_back(old);catalog["active"]=nullptr;catalog["authorization_generation"]="2";
                retention->publish(saved,catalog);
                const auto native_before=target_store->settings(),proofs_before=target_store->recurrence_authority();
                check(retention->reclaim_native_retired(*target_store)==1);
                const auto retained=retention->snapshot();check(parse_pi_pair_catalog(retained.catalog.bytes,id('a'))["retired"].empty());
                check(parse_pi_pair_operations(retained.operations.bytes,id('a'))==parse_pi_pair_operations(saved.operations.bytes,id('a')));
                check(target_store->settings()==native_before&&target_store->recurrence_authority()==proofs_before);
                check(retention->reclaim_native_retired(*target_store)==0&&retention->snapshot().authority->signature==retained.authority->signature);
                retention->finish_service();retention.reset();PiPairHistory cold_retention(root+"/closed-key-retention",id('a'));
                check(cold_retention.available()&&parse_pi_pair_catalog(cold_retention.snapshot().catalog.bytes,id('a'))["retired"].empty());
            }
            // Target-side grant and outcome owner: actual paired TLS and WAL,
            // synthetic ready-listener/retired proof isolate this owner. The
            // independent source Fleet consumer is exercised below.
            native=std::make_unique<PiLocalManagementService>(*target_store,id('a'),"Handover target",observe);
            check(native_server.start());
            auto source_retired_scope=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'))["profiles"][0]["pair"];
            source_retired_scope["state"]="revoked";source_retired_scope["current"]=nullptr;source_retired_scope["pending"]=nullptr;source_retired_scope["previous"]=nullptr;
            const auto closed_migration=Json::parse(native_request("GET",prefix+"migration").reply->payload.body);
            check(wtp_fleet_retirement_scope_closed(source_retired_scope,closed_migration));
            auto wrong_closure=closed_migration;wrong_closure["retirements"][0]["receipt_digest"]=std::string(64,'0');
            check(!wtp_fleet_retirement_scope_closed(source_retired_scope,wrong_closure));
            wrong_closure=closed_migration;wrong_closure["state"]="retiring";
            check(!wtp_fleet_retirement_scope_closed(source_retired_scope,wrong_closure));
            // Another explicit migration before any resumption retains the
            // exact permanent fence, without inventing a new signed proof.
            const auto repeat_view=Json::parse(native_request("GET",prefix+"migration").reply->payload.body);
            const auto repeat_context=Json::parse(native_request("GET",prefix+"context").reply->payload.body);
            const Json repeat_command={{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",repeat_context["binding"]},
                {"capability_revision",repeat_context["capability_revision"]},{"operation_id",id('8')},{"expected_revision",target_store->settings()["revision"]},
                {"expected_authority_generation",repeat_view["authority_generation"]},{"controllers",repeat_view["controllers"]}};
            const auto prior_fences=target_store->recurrence_authority()["retirements"];
            check(native_request("POST",prefix+"migration",repeat_command).reply->payload.status==202);
            check(target_store->remote_handover()->at("requests")[0]["state"]=="confirmed");
            native->advance_migration();check(target_store->recurrence_authority()["state"]=="staged"&&target_store->recurrence_authority()["retirements"]==prior_fences&&accepted==accepted_before+1);
            const auto disable_reference=target_store->disabled_snapshot();check(bool(disable_reference));
            const auto crash_boundaries=[&](const std::string &mode){
                native_server.stop();native.reset();
                const auto before=target_store->recurrence_authority(),before_settings=target_store->settings();target_store.reset();
                std::map<std::string,std::optional<std::string>> backup;
                for(const auto *suffix:{"",".managed-settings.json",".management-operations.json",".wtp-devices.json",".recurrence-authority.json",".legacy-recurrence.json",".recurrence-progress-initialization.json"}){
                    const auto path=target_ini+suffix;
                    if(std::filesystem::exists(path)){std::ifstream input(path,std::ios::binary);backup[path]=std::string((std::istreambuf_iterator<char>(input)),{});}else backup[path]=std::nullopt;
                }
                const auto copy_keys=[&](const std::string &from,const std::string &to,const PiPairCatalogStore::Snapshot &snapshot){
                    const auto namespace_id=Json::parse(snapshot.authority->bytes)["namespace_id"].get<std::string>();
                    std::filesystem::copy(from,to,std::filesystem::copy_options::recursive);
                    std::filesystem::copy(from+".ns-"+namespace_id,to+".ns-"+namespace_id,std::filesystem::copy_options::recursive);
                };
                for(unsigned point=0;point<4;++point){
                    const auto label=root+"/resume-crash-"+mode+"-"+std::to_string(point);
                    copy_keys(root+"/pairing",label+"-target",paired->snapshot());
                    if(mode=="ack")copy_keys(root+"/controller",label+"-source",controller->snapshot());
                    const auto selected=std::to_string(point);std::array<char *,6> arguments{argv[0],const_cast<char *>("--resume-target-crash"),
                        const_cast<char *>(root.c_str()),const_cast<char *>(mode.c_str()),const_cast<char *>(selected.c_str()),nullptr};
                    pid_t child{};check(posix_spawn(&child,argv[0],nullptr,nullptr,arguments.data(),environ)==0);int status{};
                    check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
                    {PiManagedStore cold(target_ini,id('a'),target_catalog.pi_management_anchor(id('a'),{}));cold.recover();
                        if(point<2)check(cold.recurrence_authority()==before&&cold.settings()==before_settings);
                        else if(mode=="grant")check(cold.recurrence_authority()["state"]=="reconciling"&&cold.recurrence_authority()["management"]["resume"]["grant"]["operation_id"]==id('2')&&cold.receipt(id('a'),"1",id('2'))->at("state")=="pending");
                        else check(recurrence_remote_resume_active(cold.recurrence_authority())&&cold.receipt(id('c'),"1",id('7'))->at("state")=="committed");
                        check(cold.settings()["scheduling"]["enabled"]==false&&cold.settings()["configuration"]["Operation"]["Transmit"]==false);
                    }
                    for(const auto &[path,bytes]:backup){
                        if(bytes){
                            if(std::filesystem::exists(path)&&PiManagementJournal::read_ini_input(path).bytes==*bytes)continue;
                            std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<*bytes;output.close();check(!chmod(path.c_str(),0600));
                        }else std::filesystem::remove(path);
                    }
                }
                target_store=std::make_unique<PiManagedStore>(target_ini,id('a'),target_catalog.pi_management_anchor(id('a'),{}));target_store->recover();
                check(target_store->recurrence_authority()==before&&target_store->settings()==before_settings);
                native=std::make_unique<PiLocalManagementService>(*target_store,id('a'),"Handover target",observe);check(native_server.start());
            };
            crash_boundaries("grant");
            const auto rollback_context=Json::parse(native_request("GET",prefix+"context").reply->payload.body);
            Json restore{{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",rollback_context["binding"]},
                {"capability_revision",rollback_context["capability_revision"]},{"operation_id",id('7')},
                {"expected_revision",target_store->settings()["revision"]},{"expected_authority_generation",target_store->recurrence_authority()["generation"]},
                {"resume_controller_id",id('b')},{"resume_assignment_id",source_before["assignments"][0]["assignment_id"]}};
            check(native_request("POST",prefix+"migration/rollback",restore).reply->payload.status==409);
            check(target_store->recurrence_authority()["state"]=="staged");
            ready_binding=true;
            auto bad_restore=restore;bad_restore["resume_controller_id"]=id('e');
            check(native_request("POST",prefix+"migration/rollback",bad_restore).reply->payload.status==409);
            const auto pending_restore=native_request("POST",prefix+"migration/rollback",restore);
            if(!pending_restore.reply||pending_restore.reply->payload.status!=202)throw std::runtime_error("resume prepare: "+(pending_restore.reply?pending_restore.reply->payload.body:"no reply"));
            check(target_store->recurrence_authority()["state"]=="reconciling");
            const auto issued=native_request("GET","/api/pair/v1/resume");check(issued.reply&&issued.reply->payload.status==200);
            const auto issued_grant=Json::parse(issued.reply->payload.body);check(valid_pi_pair_document(issued_grant,"resume-grant"));
            check(issued_grant["controller_incarnation"]==registration["controller_incarnation"]&&issued_grant["disabled_revision"]==(*disable_reference)["receipt"]["revision"]);
            check(native_request("POST",prefix+"migration/rollback",restore).reply->payload.status==202);
            check(PiManagementJournal::read_ini_input(target_ini).signature==ini_observation.signature);
            native_server.stop();native.reset();target_store.reset();
            target_store=std::make_unique<PiManagedStore>(target_ini,id('a'),target_catalog.pi_management_anchor(id('a'),{}));target_store->recover();
            check(target_store->recurrence_authority()["state"]=="reconciling"&&target_store->settings()["scheduling"]["enabled"]==false);
            native=std::make_unique<PiLocalManagementService>(*target_store,id('a'),"Handover target",observe);check(native_server.start());
            check(Json::parse(native_request("GET","/api/pair/v1/resume").reply->payload.body)==issued_grant);
            Json outcome{{"contract","Wsprry-Pair/1"},{"schema","resume-ack"},{"operation_id",issued_grant["operation_id"]},
                {"pair_id",issued_grant["pair_id"]},{"authorization_generation",issued_grant["authorization_generation"]},
                {"target_id",id('a')},{"controller_id",id('b')},{"assignment_id",issued_grant["assignment_id"]},{"grant_digest",issued_grant["grant_digest"]},
                {"outcome","resumed"},{"controller_generation","4"},{"receipt_digest",std::string(64,'8')}};
            const auto signed_outcome=controller->resume_acknowledgement(outcome);check(bool(signed_outcome));
            auto forged=*signed_outcome;forged["proof"]=std::string(43,'A');
            check(native_request("POST","/api/pair/v1/resume/ack",forged).reply->payload.status==403&&target_store->recurrence_authority()["state"]=="reconciling");
            const auto pending_migration=native_request("GET",prefix+"migration"),pending_settings=native_request("GET",prefix+"settings");
            check(pending_settings.principal&&pending_settings.reply&&pending_migration.reply);
            const auto pending_proof=controller->outgoing_resume_grant(id('9'),*pending_settings.principal,issued_grant,
                Json::parse(pending_migration.reply->payload.body),Json::parse(pending_settings.reply->payload.body));check(bool(pending_proof));
            const auto held_authority=target_store->recurrence_authority(),held_settings=target_store->settings();
            const auto held_source=Json::parse(wtp_fleet_api("GET",{},{},"retirements").body);
            check(!wtp_fleet_resume(*pending_proof,*controller,[]{return false;}));
            check(target_store->recurrence_authority()==held_authority&&target_store->settings()==held_settings);
            check(Json::parse(wtp_fleet_api("GET",{},{},"retirements").body)==held_source&&source_contacts==0);
            // An old target may already have durably issued a handback grant.
            // Current software must leave it held, never recreate dispatch or
            // fabricate a successful resume acknowledgement.
            WtpCatalog source_catalog(source_ini+".devices.json");
            PiPairRemoteController source_worker([&]{return controller;},requests,id('b'),source_catalog,source_ini+".management",
                PiPairRemoteController::LoopbackTest{{"127.0.0.1",native_server.port()}});
            source_worker.authority_source([]{return wtp_fleet_api("GET",{},{},"retirements");});
            check(source_worker.observe(id('9')));(void)source_worker.maintain(id('9'));
            check(target_store->recurrence_authority()["state"]=="reconciling"&&target_store->settings()["scheduling"]["enabled"]==false);
            check(!recurrence_remote_resume_active(target_store->recurrence_authority())&&source_contacts==0);
            check(PiManagementJournal::read_ini_input(target_ini).signature==ini_observation.signature);
            if(refuse_second){
                binding_port=31419;
                check(!wtp_fleet_resume(*pending_proof,*controller,[]{return false;}));
                check(target_store->settings()==held_settings&&source_contacts==0);
            }
            check(!engine.output_active());actual_handover_source=false;
            native_server.stop();
        }
        verifier.stop();
        {
            const auto ini=root+"/retirement-source-fleet";WtpSettings settings;settings.transport="network_plain";settings.hostname="wspr4.local";
            settings.tcp_port=31417;settings.device_id=id('a');settings.allow_frequency_adjustment=true;
            const Json row={{"device_id",id('a')},{"name","Retirement source"},{"settings",wtp_settings_json(settings)},
                {"product","WsprryPi"},{"revocation_generation","7"},{"management_port",31415},{"enabled",false},
                {"last_start_ns","0"},{"in_flight",false},{"schedule",{{"mode","tone"},{"frequency_hz",14097100},
                {"duration_ms",1000},{"period_seconds",60},{"phase_seconds",0}}}};
            {WtpFleetStore saved(ini+".wtp-assignments.json",id('b'));saved.update(saved.snapshot().second,[&](Json &data){data={{"version",1},{"assignments",Json::array({row})},{"removals",Json::array()}};});}
            std::atomic_uint attempted_carrier{};WtpFleetTestHooks hooks;hooks.retirement_directory=ini+".retirements";
            hooks.before_execution_contact=[&]{++attempted_carrier;throw std::runtime_error("Retired execution contact");};

            start_wtp_fleet_for_test(ini,id('b'),hooks);struct StopFleet{~StopFleet(){stop_wtp_fleet();}} stop_fleet;
            const auto before=strict_browser_json(wtp_fleet_api("GET",{},{},"retirements").body);
            auto command=unsigned_request;command["assignment_id"]=before["assignments"][0]["assignment_id"];
            command["assignment_revision"]=before["assignments"][0]["assignment_revision"];
            const auto signed_command=paired->retirement_request(command);check(bool(signed_command));
            WtpCatalog retirement_catalog(ini+".devices.json");
            const auto active=[] {return std::optional<WtpSettings>{};};
            auto picos=std::make_shared<PicoTargetController>(retirement_catalog,active,ini+".picos",id('b'));
            PiPairMachineBackend backend([&]{return controller;},retirement_catalog,picos,active);
            PiPairMachineService scoped(*controller,callback_sessions,id('b'),[&](const auto &,const auto &){++ordinary_calls;return controller_target_error(503,"authority_pending");},
                [&](const auto &principal,const auto &request,const auto &cancelled){
                    return backend.request(principal,request,cancelled);
                });
            PiPairServer source_server(callbacks,[&](const auto &identity){return controller->retirement_server_credential(identity);},
                [&](const auto &principal){return controller->retirement_finished(principal,true);},
                [&](const auto &principal,const auto &request,auto &connection,const auto &cancelled){return scoped.handle(principal,request,connection,cancelled);},PiPairServer::Access::LoopbackTest);
            check(source_server.start());
            const auto send=[&](const std::string &method,const std::string &path,const Json &body){return target.exchange({"127.0.0.1",source_server.port()},id('b'),
                [&](const auto &identity){return paired->retirement_client_credential(identity);},retirement_identity,
                [&](const auto &principal){return paired->retirement_finished(principal,false);},method,path,body.is_null()?std::string{}:body.dump());};
            auto stale_command=command;stale_command["assignment_revision"]=std::string(64,'0');
            const auto stale_signed=paired->retirement_request(stale_command);check(bool(stale_signed));
            const auto stale_reply=send("POST","/api/pair/v1/retirement",*stale_signed);
            check(stale_reply.reply&&stale_reply.reply->payload.status==409&&valid_pi_pair_document(strict_browser_json(stale_reply.reply->payload.body),"result"));
            check(strict_browser_json(wtp_fleet_api("GET",{},{},"retirements").body)==before);
            const auto accepted_reply=send("POST","/api/pair/v1/retirement",*signed_command);
            check(accepted_reply.reply&&accepted_reply.reply->payload.status==409);
            const auto held_result=parse_pi_pair_document(accepted_reply.reply->payload.body,"result");
            check(held_result["code"]=="busy"&&held_result["retryable"]==false);
            check(strict_browser_json(wtp_fleet_api("GET",{},{},"retirements").body)==before);
            auto status=command;status["schema"]="retirement-status";status.erase("assignment_revision");status.erase("retirement_generation");
            const auto signed_status=paired->retirement_request(status);check(bool(signed_status));
            const auto readback=send("POST","/api/pair/v1/retirement/status",*signed_status);
            check(readback.reply&&readback.reply->payload.status==404&&parse_pi_pair_document(readback.reply->payload.body,"result")["code"]=="operation_unknown");
            for(const auto &path:{"/api/pair/v1/resume/ack","/api/pair/v1/authorities/register","/api/pair/v1/rotate/prepare","/api/control/v1/targets"}){
                const auto denied=send("POST",path,*signed_command);check(denied.reply&&denied.reply->payload.status==403);
            }
            check(ordinary_calls==0);
            source_server.stop();stop_wtp_fleet();
            start_wtp_fleet_for_test(ini,id('b'),hooks);
            auto restarted=strict_browser_json(wtp_fleet_api("GET",{},{},"retirements").body);
            check(restarted["controller_incarnation"]!=before["controller_incarnation"]);
            check(restarted["revision"]!=before["revision"]);
            auto prior=before;prior.erase("controller_incarnation");restarted.erase("controller_incarnation");
            prior.erase("revision");restarted.erase("revision");
            check(restarted==prior);
            check(attempted_carrier==0); // Current software never starts legacy cleanup or signs a guessed idle proof.
            stop_wtp_fleet();
            // Existing source-side browser retirement must not strand a later
            // authenticated machine handover. Keep its old receipt intact.
            const auto retired_ini=root+"/already-retired-source",retired_directory=retired_ini+".retirements";
            {WtpFleetStore saved(retired_ini+".wtp-assignments.json",id('b'));saved.update({},[&](Json &data){data["assignments"].push_back(row);});}
            Json old_receipt,old_book;
            {ControllerRetirementBook book(retired_directory,retired_ini+".wtp-assignments.json",id('b'),Json::array({row}));
             const auto snapshot=book.read(id('d'),Json::array({row}));
             const Json intent={{"contract","Wsprry-Control/1"},{"schema","controller-retirement"},{"controller_id",id('b')},{"controller_incarnation",id('d')},
                {"operation_id",id('1')},{"transition_id",id('2')},{"target_id",id('a')},{"assignment_id",snapshot["assignments"][0]["assignment_id"]},
                {"expected_assignment_revision",snapshot["assignments"][0]["assignment_revision"]},{"expected_retirement_generation",snapshot["generation"]}};
             book.retire(intent,snapshot["revision"],id('d'),Json::array({row}));
             book.confirm_idle(id('a'),{{"device_id",id('a')},{"boot_id",id('f')},{"owner_id",nullptr},{"job_id",nullptr},{"output_active",false},
                {"state","empty"},{"submitted_job_id",nullptr},{"session_id",nullptr},{"controller_owner_id",nullptr}});
             old_book=book.read(id('d'),Json::array({row}));old_receipt=old_book["assignments"][0]["receipt"];}
            hooks.retirement_directory=retired_directory;const auto contacts_before=attempted_carrier.load();
            start_wtp_fleet_for_test(retired_ini,id('b'),hooks);check(source_server.start());
            auto request=unsigned_request;request["transition_id"]=id('e');request["retirement_generation"]="8";
            request["assignment_id"]=old_book["assignments"][0]["assignment_id"];request["assignment_revision"]=old_book["assignments"][0]["assignment_revision"];
            const auto frozen=paired->retirement_request(request);check(bool(frozen));
            PiPairRetirementClient retired_client(*paired,requests,PiPairRetirementClient::LoopbackTest{{"127.0.0.1",source_server.port()}});
            const PiPairEndpoint registered{"wsprry-management-"+id('b')+".local.",31418};using Mode=PiPairRetirementClient::Mode;using State=PiPairRetirementClient::State;
            unsigned handoffs=0;const auto verify_only=[&](const auto &){++handoffs;return false;};
            const auto absent=retired_client.exchange(registered,*frozen,Mode::Status,verify_only);check(absent.state==State::Unknown&&handoffs==0);
            PiManagementJournal handoff(retired_ini+".target-handoff-wal",id('a'),{{"recurrence",retired_ini+".target-handoff.json"}});
            const auto result=retired_client.exchange(registered,*frozen,Mode::Request,[&](const auto &){++handoffs;
                if(!handoff.inspect("recurrence")){handoff.prepare({{"recurrence",std::nullopt,frozen->dump()}});handoff.commit();handoff.recover();}return true;});
            check(result.state==State::Retired&&result.proof&&handoffs>0);
            check(result.proof->document()["transition_id"]==id('e')&&result.proof->document()["receipt_digest"]==PiManagementJournal::digest(canonical_pi_management_json(old_receipt)));
            const auto saved=strict_browser_json(wtp_fleet_api("GET",{},{},"retirements").body);
            check(saved["assignments"][0]["receipt"]==old_receipt&&saved["assignments"][0]["state"]=="retired"&&attempted_carrier==contacts_before);
            const auto count=handoffs;const auto recovered=retired_client.exchange(registered,*frozen,Mode::Status,verify_only);
            check(recovered.state==State::Retired&&recovered.proof&&handoffs==count&&attempted_carrier==contacts_before);
            // Isolate source grant consumption using synthetic closed disabled
            // and reservation readbacks, with a real current normal-TLS
            // principal and the actual source Fleet/book/assignment/WAL owner.
            // Target grant issuance is a separate unfinished integration gate.
            PiPairClient manager(requests,PiPairClient::Access::LoopbackTest);
            const auto actual=manager.exchange({"127.0.0.1",server.port()},id('a'),[&](const auto &identity){return controller->outgoing_credential(id('9'),identity);},tls_identity,
                [&](const auto &principal){return controller->record_outgoing_finished(id('9'),principal);},"GET","/api/control/v1/targets/"+id('a')+"/settings");
            check(actual.principal&&actual.reply&&actual.reply->payload.status==200);
            auto disabled=Json::parse(actual.reply->payload.body);disabled["saved"]["scheduling"]["enabled"]=false;
            disabled["candidate_digest"]=PiManagementJournal::digest(canonical_pi_management_json(disabled["saved"]));
            const auto live_book=Json::parse(wtp_fleet_api("GET",{},{},"retirements").body);
            Json grant={{"contract","Wsprry-Pair/1"},{"schema","resume-grant"},{"operation_id",id('7')},{"pair_id",id('c')},{"authorization_generation","1"},
                {"target_id",id('a')},{"target_boot_id",disabled["binding"]["boot_id"]},{"controller_id",id('b')},{"controller_incarnation",live_book["controller_incarnation"]},
                {"assignment_id",request["assignment_id"]},{"assignment_revision",request["assignment_revision"]},{"transition_id",request["transition_id"]},
                {"retirement_receipt_digest",PiManagementJournal::digest(canonical_pi_management_json(old_receipt))},{"authority_generation","9"},
                {"disabled_revision",disabled["revision"]},{"disable_receipt_digest",std::string(64,'d')},{"candidate_digest",disabled["candidate_digest"]},
                {"wtp_binding",{{"revocation_generation","8"},{"wtp_port",31419},{"management_port",31416}}}};
            grant["grant_digest"]=PiManagementJournal::digest(canonical_pi_management_json(grant));
            Json reservation={{"contract","Wsprry-Control/1"},{"schema","target-migration"},{"binding",disabled["binding"]},{"authority_generation","9"},
                {"transition_id",grant["operation_id"]},{"state","reconciling"},
                {"controllers",Json::array({{{"controller_id",id('b')},{"assignment_id",request["assignment_id"]},{"assignment_revision",request["assignment_revision"]}}})},
                {"retirements",Json::array({{{"controller_id",id('b')},{"assignment_id",request["assignment_id"]},{"transition_id",request["transition_id"]},
                    {"retirement_generation","8"},{"receipt_digest",grant["retirement_receipt_digest"]}}})},{"jobs",Json::array()},{"staged_revision",nullptr},{"wtp_binding",grant["wtp_binding"]}};
            check(valid_controller_document(disabled,"target-settings")&&valid_controller_document(reservation,"target-migration-read"));
            const auto proof=controller->outgoing_resume_grant(id('9'),*actual.principal,grant,reservation,disabled);
            check(proof&&proof->original_scope()&&proof->current_target());
            auto malformed=reservation;malformed["extra"]=true;check(!controller->outgoing_resume_grant(id('9'),*actual.principal,grant,malformed,disabled));
            auto wrong_digest=disabled;wrong_digest["candidate_digest"]=std::string(64,'0');check(!controller->outgoing_resume_grant(id('9'),*actual.principal,grant,reservation,wrong_digest));
            const auto source_before=PiManagementJournal::read_private_record(retired_ini+".wtp-assignments.json");check(bool(source_before));
            const auto before_book=Json::parse(wtp_fleet_api("GET",{},{},"retirements").body);
            check(!wtp_fleet_resume(*proof,*controller,[]{return false;}));
            const auto after=PiManagementJournal::read_private_record(retired_ini+".wtp-assignments.json");
            check(after&&after->signature==source_before->signature&&attempted_carrier==contacts_before);
            check(Json::parse(wtp_fleet_api("GET",{},{},"retirements").body)==before_book);
            auto changed_boot=reservation;changed_boot["binding"]["boot_id"]=id('0');auto changed_settings=disabled;changed_settings["binding"]=changed_boot["binding"];
            const auto recovery=controller->outgoing_resume_grant(id('9'),*actual.principal,grant,changed_boot,changed_settings);check(recovery&&!recovery->current_target());
            check(!wtp_fleet_resume(*recovery,*controller,[]{return false;})&&attempted_carrier==contacts_before);
            source_server.stop();stop_wtp_fleet();
        }
    }
    {
        // The protected browser owner retains an actual service Call through
        // its remote TLS exchange. Listener failure must leave local owners
        // alone, while a known remote can never fall through to them.
        std::shared_ptr<PiPairHistory> web_history=history(root+"/web-controller",true);
        PiPairAdmission web_admission;ManagementBrowserSessions web_sessions;PiPairDiscovery discovery(id('b'));
        WtpCatalog web_catalog(root+"/web-controller-devices.json");
        auto remote=std::make_shared<PiPairRemoteController>([&]{return web_history;},web_admission,id('b'),web_catalog,root+"/web-management",
            PiPairRemoteController::LoopbackTest{{"127.0.0.1",server.port()}});
        check(remote->observe(id('9')));web_history->finish_service();
        PiPairLocalService browser(web_history,web_sessions,web_admission,discovery,id('b'),id('f'),remote);check(browser.begin());
        const SupportRequestGuardSnapshot guard{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}};
        httplib::Request request;request.method="GET";request.path="/api/control/v1/targets";request.target=request.path;
        request.remote_addr="192.168.1.20";request.headers={{"Host","device.local"},{"Origin","http://device.local"}};
        check(!browser.remote(request,guard));
        request.path="/api/control/v1/targets/"+id('a')+"/context";request.target=request.path;
        check(browser.remote(request,guard)->payload.status==503);
        request.path="/api/control/v1/targets/"+id('b')+"/context";request.target=request.path;check(!browser.remote(request,guard));
        check(browser.transport_ready());
        request.path="/api/control/v1/targets/"+id('a')+"/context";request.target=request.path;
        auto reply=browser.remote(request,guard);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(reply&&reply->payload.status!=200&&std::chrono::steady_clock::now()<deadline){std::this_thread::sleep_for(std::chrono::milliseconds(110));reply=browser.remote(request,guard);}
        check(reply&&reply->payload.status==200&&valid_controller_document(Json::parse(reply->payload.body),"target-context"));
        auto forged=request;forged.headers.erase("Host");forged.headers.emplace("Host","foreign.local");check(browser.remote(forged,guard)->payload.status==403);
        forged=request;forged.headers.emplace("X-WsprryPi-Profile-ID",id('9'));forged.headers.emplace("X-WsprryPi-Profile-ID",id('9'));
        check(browser.remote(forged,guard)->payload.status==400);
        forged=request;forged.method="PUT";forged.path="/api/control/v1/targets/"+id('a')+"/settings";forged.target=forged.path;
        forged.headers.emplace("Content-Type","application/json");forged.headers.emplace("X-WsprryPico-Request","1");forged.body="{}";
        check(browser.remote(forged,guard)->payload.status==403);
        browser.request_stop();check(browser.remote(request,guard)->payload.status==503);browser.stop_and_drain();
    }
    PiPairClient client(outgoing,PiPairClient::Access::LoopbackTest);
    const auto exchange=[&](const std::string &resource,const std::string &method="GET",const Json &body=nullptr,const std::string &revision=""){
        httplib::Headers metadata;if(!revision.empty())metadata.emplace("If-Match",revision);bool recorded=false,checkpoint=false;
        const PiPairClient::WriteAdmission admission=method=="GET"?PiPairClient::WriteAdmission{}:PiPairClient::WriteAdmission([&](const auto &principal){
            auto guard=controller->admit_outgoing_mutation(id('9'),profile_generation,principal,!checkpoint);if(guard)checkpoint=true;return guard;});
        std::this_thread::sleep_for(std::chrono::milliseconds(110));
        auto response=client.exchange({"127.0.0.1",server.port()},id('a'),[&](const auto &requested){return controller->outgoing_credential(id('9'),requested);},tls_identity,
            [&](const auto &principal){if(!recorded)recorded=controller->record_outgoing_finished(id('9'),principal);return recorded;},
            method,resource.starts_with("/api/")?resource:"/api/control/v1/targets/"+id('a')+"/"+resource,body.is_null()?"":body.dump(),metadata,[]{return false;},admission);
        if(method=="GET"&&!response.reply&&removal_receipt.is_null())throw std::runtime_error("missing target response: "+resource+" state "+std::to_string(static_cast<int>(response.state)));
        if(response.reply&&response.reply->payload.body.empty())throw std::runtime_error("empty target response: "+resource+" status "+std::to_string(response.reply->payload.status));
        return response;
    };
    {
        const auto before=store.settings(),authority_before=store.recurrence_authority();
        const auto ini_before=PiManagementJournal::read_ini_input(ini);
        Json registration{{"contract","Wsprry-Pair/1"},{"schema","authority-register"},{"operation_id",id('7')},{"pair_id",id('c')},
            {"authorization_generation","1"},{"target_id",id('a')},{"controller_id",id('b')},{"controller_incarnation",id('d')},{"controller_generation","1"},
            {"assignments",Json::array({{{"assignment_id",id('9')},{"assignment_revision",std::string(64,'e')},{"state","legacy"}}})},
            {"endpoint",{{"host","wsprry-management-"+id('b')+".local."},{"port",31418}}}};
        const auto registered=exchange("/api/pair/v1/authorities/register","POST",registration);
        check(registered.reply&&registered.reply->payload.status==200);
        const auto receipt=Json::parse(registered.reply->payload.body);check(receipt["state"]=="committed"&&receipt["operation_id"]==id('7'));
        const auto after=store.settings();auto authority=store.recurrence_authority();
        check(after["configuration"]==before["configuration"]&&after["scheduling"]==before["scheduling"]&&after["candidate_digest"]==before["candidate_digest"]&&
            after["configuration_generation"]==before["configuration_generation"]&&after["revision"]!=before["revision"]);
        check(PiManagementJournal::read_ini_input(ini).signature==ini_before.signature&&authority["management"]["complete"]==false);
        authority.erase("management");check(authority==authority_before);
        const auto replay=exchange("/api/pair/v1/authorities/register","POST",registration);
        check(replay.reply&&replay.reply->payload.body==registered.reply->payload.body&&store.settings()==after);
        const auto lookup=exchange("/api/pair/v1/operations/"+id('7'));check(lookup.reply&&lookup.reply->payload.body==registered.reply->payload.body);
        auto wrong=registration;wrong["operation_id"]=id('8');wrong["controller_id"]=id('e');
        check(exchange("/api/pair/v1/authorities/register","POST",wrong).reply->payload.status==403&&store.settings()==after);
        wrong=registration;wrong["operation_id"]=id('8');wrong["endpoint"]["host"]="127.0.0.1";
        check(exchange("/api/pair/v1/authorities/register","POST",wrong).reply->payload.status==400&&store.settings()==after);
        wrong=registration;wrong["operation_id"]=id('c');check(exchange("/api/pair/v1/authorities/register","POST",wrong).reply->payload.status==409&&store.settings()==after);
        wrong=registration;wrong["assignments"][0]["assignment_revision"]=std::string(64,'f');
        check(exchange("/api/pair/v1/authorities/register","POST",wrong).reply->payload.status==409&&store.settings()==after);
    }
    {
        WtpSettings settings;settings.transport="network_plain";settings.hostname="target.local";settings.tcp_port=31417;settings.device_id=id('a');
        Json rows=Json::array({{{"device_id",id('a')},{"name","Actual legacy assignment"},{"settings",wtp_settings_json(settings)},{"product","WsprryPi"},
            {"revocation_generation","0"},{"management_port",31415},{"enabled",false},{"last_start_ns","0"},{"in_flight",false},
            {"schedule",{{"mode","tone"},{"frequency_hz",14097100},{"duration_ms",1000},{"period_seconds",60},{"phase_seconds",0}}}}});
        ControllerRetirementBook source(root+"/actual-source-book",root+"/actual-source-assignments.json",id('b'),rows);
        PiPairRemoteController owner([&]{return controller;},outgoing,id('b'),remote_catalog,root+"/remote-management",PiPairRemoteController::LoopbackTest{{"127.0.0.1",server.port()}});
        owner.authority_source([&]{const auto value=source.read(id('f'),rows);return PicoHttpResponse{200,value.dump(),"\""+value["revision"].get<std::string>()+"\""};});
        owner.receipt_eviction_observer([&]{sessions.retire();});
        check(owner.observe(id('9')));const auto before=store.settings();const auto posts_before=registration_requests;lose_registration_reply=true;
        (void)owner.maintain(id('9')); // inventory turn may be rate-limited
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        bool recorded=false;
        while(!recorded&&std::chrono::steady_clock::now()<deadline){
            std::this_thread::sleep_for(std::chrono::milliseconds(150));(void)owner.maintain(id('9'));
            const auto authority=store.recurrence_authority();for(const auto &entry:authority["management"]["authorities"])
                if(entry["registration"]["controller_incarnation"]==id('f'))recorded=true;
        }
        check(recorded&&!lose_registration_reply&&registration_requests==posts_before+1);
        const auto interrupted=parse_pi_pair_operations(controller->snapshot().operations.bytes,id('b'));bool retained=false;
        for(const auto &entry:interrupted["entries"])if(!entry["continuation"].is_null()&&entry["continuation"]["kind"]=="authority-registration-intent")retained=entry["unresolved"]==true&&entry["continuation"]["stage"]=="awaiting_receipt";
        check(retained);const auto after=store.settings();check(after["configuration_generation"]==before["configuration_generation"]&&after["candidate_digest"]==before["candidate_digest"]);
        // Destruction intentionally leaves the running marker. The separate
        // process must renew, load the exact intent, and query its receipt.
        controller.reset();const auto port=std::to_string(server.port());
        std::array<char *,5> arguments{argv[0],const_cast<char *>("--recover-authority-registration"),const_cast<char *>(root.c_str()),const_cast<char *>(port.c_str()),nullptr};
        pid_t child{};check(posix_spawn(&child,argv[0],nullptr,nullptr,arguments.data(),environ)==0);int status{};
        check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
        controller=std::make_shared<PiPairHistory>(root+"/controller",id('b'));check(controller->available()&&!controller->unclean_start()&&controller->begin_service());
        const auto renewed=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'))["profiles"][0]["pair"];
        tls_identity.epoch=renewed["current"]["epoch"].get<std::string>();check(tls_identity.epoch=="2");
        prepares=0;commits=0;rotation_operation.clear();
        check(registration_requests==posts_before+1);
        const auto operations=parse_pi_pair_operations(controller->snapshot().operations.bytes,id('b'));bool completed=false;
        for(const auto &entry:operations["entries"])if(entry["scope"]=="device"&&entry["state"]=="committed"&&entry["request_digest"]==entry["result_digest"])completed=true;
        check(completed);check(store.recurrence_authority()["management"]["complete"]==false);
    }
    const auto context=exchange("context"),settings=exchange("settings");check(context.reply&&settings.reply&&context.reply->payload.status==200&&settings.reply->payload.status==200);
    const auto bound=Json::parse(context.reply->payload.body),saved=Json::parse(settings.reply->payload.body);
    check(bound["authorization"]==Json{{"pair_id",id('c')},{"authorization_generation","1"},{"renewal_required",false}}&&bound["gateway"].is_null());
    Json save{{"contract","Wsprry-Control/1"},{"schema","target-save"},{"binding",bound["binding"]},{"capability_revision",bound["capability_revision"]},
        {"operation_id",id('1')},{"expected_revision",saved["revision"]},{"candidate",saved["saved"]}};
    save["candidate"]["configuration"]["CW"]["Message"]="PAIRED SAVE";
    const auto native_route="/api/control/v1/targets/"+id('a')+"/settings";
    check(!remote_journal->reserve(id('c'),native_route,"PUT",bound["binding"],"1",id('1'),pi_managed_request_digest(id('a'),id('c'),"1",native_route,save)));
    const auto accepted=exchange("settings","PUT",save,settings.reply->payload.etag);
    check(accepted.reply&&accepted.reply->payload.status==200&&Json::parse(accepted.reply->payload.body)["state"]=="committed");
    check(!remote_journal->finish(id('1'),accepted.reply->payload)&&remote_journal->receipt(id('1'))==Json::parse(accepted.reply->payload.body));
    check(store.receipt(id('c'),"1",id('1')).has_value()&&!store.receipt(id('a'),"1",id('1'))&&!store.receipt(id('b'),"1",id('1')));
    const auto exact=paired->snapshot().authority->signature;auto revision=store.settings()["revision"];
    const auto replay=exchange("settings","PUT",save,settings.reply->payload.etag);check(replay.reply&&replay.reply->payload.body==accepted.reply->payload.body&&paired->snapshot().authority->signature==exact&&store.settings()["revision"]==revision);
    auto fresh=Json::parse(exchange("settings").reply->payload.body);save["operation_id"]=id('2');save["expected_revision"]=fresh["revision"];save["candidate"]=fresh["saved"];
    save["candidate"]["configuration"]["CW"]["Message"]="REFUSED OFF";turn_off=true;
    const auto denied=exchange("settings","PUT",save,"\""+fresh["revision"].get<std::string>()+"\"");
    check(denied.reply&&denied.reply->payload.status==409&&Json::parse(denied.reply->payload.body)["error"]["code"]=="authority_pending"&&!store.receipt(id('c'),"1",id('2'))&&store.settings()["revision"]==revision);
    check(exchange("context").reply->payload.status==403);set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);
    server.stop();machine.reset();paired.reset();paired=std::make_unique<PiPairHistory>(root+"/pairing",id('a'));check(paired->unclean_start());paired->begin_service();machine=make_machine();check(server.start());
    const auto due_result=exchange("context");const auto due=Json::parse(due_result.reply->payload.body);check(due["authorization"]["renewal_required"]==true&&due["binding"]==bound["binding"]);
    save["operation_id"]=id('3');save["candidate"]["configuration"]["CW"]["Message"]="AFTER AUTOMATIC RENEWAL";
    const auto held=exchange("settings","PUT",save,"\""+fresh["revision"].get<std::string>()+"\"");
    check(held.reply&&held.reply->payload.status==409&&!store.receipt(id('c'),"1",id('3'))&&store.settings()["revision"]==revision);
    ManagementBrowserSessions controller_sessions;
    auto renewal=std::make_unique<PiPairRenewal>(*controller,controller_sessions,outgoing,id('b'),PiPairRenewal::LoopbackTest{{"127.0.0.1",server.port()}});
    const auto restart_controller=[&]{renewal.reset();controller.reset();controller=std::make_unique<PiPairHistory>(root+"/controller",id('b'));check(controller->unclean_start());controller->begin_service();
        renewal=std::make_unique<PiPairRenewal>(*controller,controller_sessions,outgoing,id('b'),PiPairRenewal::LoopbackTest{{"127.0.0.1",server.port()}});};
    check(due_result.principal&&renewal->target_requires(id('9'),*due_result.principal,due));
    auto malformed=due;malformed["authorization"]["renewal_required"]="true";check(!renewal->target_requires(id('9'),*due_result.principal,malformed));
    malformed=due;malformed["authorization"]["pair_id"]=id('d');check(!renewal->target_requires(id('9'),*due_result.principal,malformed));
    malformed=due;malformed["target"]["device_id"]=id('b');check(!renewal->target_requires(id('9'),*due_result.principal,malformed));
    const auto prior_epoch=tls_identity.epoch;
    lose_prepare=true;lose_commit=true;
    check(renewal->advance(id('9'))==PiPairRenewal::Result::Pending&&!lose_prepare&&prepares==1);
    restart_controller();check(renewal->advance(id('9'))==PiPairRenewal::Result::Pending&&prepares==2);
    check(controller->outgoing_rotation_request(id('9'))->at("schema")=="rotate-commit");restart_controller();
    check(renewal->advance(id('9'))==PiPairRenewal::Result::Pending&&commits==0);
    check(renewal->advance(id('9'))==PiPairRenewal::Result::Pending&&!lose_commit&&commits==1);restart_controller();
    {
        PiPairWindow local_window(*controller,controller_sessions,id('b'),id('f'),[&](const auto &approval){return controller->commit_pair(approval);});
        PiPairDiscovery discovery(id('b'));PiPairConnect recovery(*controller,controller_sessions,discovery,outgoing,id('b'),id('f'));
        PiPairMaintenance maintenance(*controller,local_window,recovery,*renewal,id('b'));
        check(maintenance.start()&&!maintenance.start());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(controller->outgoing_rotation_request(id('9'))&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        maintenance.stop();maintenance.stop();
        check(!controller->outgoing_rotation_request(id('9'))&&renewal->advance(id('9'))==PiPairRenewal::Result::Ready);
        check(nlohmann::json::parse(controller->snapshot().authority->bytes)["clean_shutdown"]==false);
    }
    const auto stable=controller->snapshot().authority->signature;check(renewal->advance(id('9'))==PiPairRenewal::Result::Ready&&controller->snapshot().authority->signature==stable&&prepares==2&&commits==1);
    const auto renewed=parse_pi_pair_peers(controller->snapshot().peers->bytes,id('b'))["profiles"][0]["pair"];
    tls_identity.epoch=renewed["current"]["epoch"].get<std::string>();check(tls_identity.epoch==std::to_string(std::stoull(prior_epoch)+1)&&renewed["pending"].is_null()&&renewed["previous"].is_null());
    const auto fresh_context=Json::parse(exchange("context").reply->payload.body);check(fresh_context["authorization"]["renewal_required"]==false&&fresh_context["binding"]==bound["binding"]);
    const auto after_renewal=exchange("settings","PUT",save,"\""+fresh["revision"].get<std::string>()+"\"");check(after_renewal.reply&&after_renewal.reply->payload.status==200&&store.receipt(id('c'),"1",id('3')));
    fresh=Json::parse(exchange("settings").reply->payload.body);revision=fresh["revision"];save["expected_revision"]=revision;save["candidate"]=fresh["saved"];
    // A valid current principal cannot commit after local revocation during
    // native reservation, even though its TLS Finished happened earlier.
    revoke=true;save["operation_id"]=id('4');const auto removed=exchange("settings","PUT",save,"\""+fresh["revision"].get<std::string>()+"\"");
    check(removed.reply&&removed.reply->payload.status==409&&!store.receipt(id('c'),"1",id('4'))&&store.settings()["revision"]==revision);
    httplib::Request read;read.method="GET";read.path="/api/control/v1/host/pairing/operations/"+id('5');read.target=read.path;read.remote_addr="192.168.1.20";read.headers={{"Host","device.local"}};
    const auto local_read=authorize_local_pairing_read(read,{true,"device.local",{},{{"192.168.1.10","255.255.255.0"}}});check(local_read.has_value());
    const auto before_read=paired->snapshot().authority->signature;
    const auto receipt=paired->local_pairing_receipt(*wtp_browser_http_request(read),*local_read);
    check(receipt.status==200&&Json::parse(receipt.body)==removal_receipt&&paired->snapshot().authority->signature==before_read);
    check(removal_receipt["binding"]==Json{{"device_id",id('a')},{"boot_id",id('f')},{"profile_generation","1"},{"connection_generation","1"}}&&removal_receipt["state"]=="committed");
    const auto revoked=parse_pi_pair_catalog(paired->snapshot().catalog.bytes,id('a'));
    check(revoked["authorization_generation"]=="2"&&revoked["active"].is_null()&&revoked["retired"].size()==1&&
        revoked["retired"][0]["current"].is_null()&&revoked["retired"][0]["pending"].is_null()&&revoked["retired"][0]["previous"].is_null()&&!revoked["retired"][0]["retirement_verifier"].is_null());
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::insecure_disabled);
    check(paired->local_pairing_receipt(*wtp_browser_http_request(read),*local_read).status==403&&paired->snapshot().authority->signature==before_read);
    set_privileged_network_runtime_mode(PrivilegedNetworkMode::enforced);
    retirement_keys(); // Removal keeps only the separately purposed verifier.
    check(exchange("context").state==PiPairClientResult::State::Unavailable);
    // Seed a new, already-paired manager scope in this fixture. Pair/PAKE
    // lifecycle has separate production tests; these exchanges still require
    // actual new-key Finished and both real current owners. A pre-removal draft
    // must not acquire new-manager authority merely by changing the TLS key.
    {
        const auto before=paired->snapshot();auto catalog=parse_pi_pair_catalog(before.catalog.bytes,id('a'));
        auto replacement=catalog["retired"][0];replacement["pair_id"]=id('d');replacement["authorization_generation"]="3";replacement["state"]="active";replacement["receipt"]=nullptr;
        replacement["current"]={{"epoch","1"},{"tls_psk",key().storage_encoding()},{"service_ms","0"},{"connections","0"}};
        std::array<unsigned char,32> fresh_retirement_bytes{};fresh_retirement_bytes.fill(8);
        replacement["retirement_verifier"]=PiPairSecret(fresh_retirement_bytes).storage_encoding();
        catalog["active"]=replacement;catalog["authorization_generation"]="3";paired->publish(before,catalog);
        const auto outgoing_before=controller->snapshot();auto peers=parse_pi_pair_peers(outgoing_before.peers->bytes,id('b'));
        auto previous=peers["profiles"][0];previous["pair"]["state"]="revoked";previous["pair"]["current"]=nullptr;previous["pair"]["pending"]=nullptr;previous["pair"]["previous"]=nullptr;
        peers["retired"].push_back({{"profile_id",previous["profile_id"]},{"target_id",previous["target_id"]},{"endpoint",previous["endpoint"]},{"pair",previous["pair"]}});
        replacement["self_id"]=id('b');replacement["peer_id"]=id('a');peers["profiles"][0]["pair"]=replacement;peers["profiles"][0]["profile_generation"]="2";
        controller->publish(outgoing_before,parse_pi_pair_catalog(outgoing_before.catalog.bytes,id('b')),parse_pi_pair_operations(outgoing_before.operations.bytes,id('b')),peers);
        profile_generation="2";tls_identity={PiPairTlsPurpose::Management,id('d'),"3","1"};
        const auto before_save=store.settings()["revision"];
        const auto stale=exchange("settings","PUT",save,"\""+fresh["revision"].get<std::string>()+"\"");
        if(stale.reply&&stale.reply->payload.status==200&&store.settings()["revision"]!=before_save&&store.receipt(id('d'),"3",id('4')))
            std::cerr<<"REPRO: pre-removal native draft committed under new manager scope\n";
        check(stale.reply&&stale.reply->payload.status==409&&store.settings()["revision"]==before_save&&!store.receipt(id('d'),"3",id('4')));
        check(controller->resume_acknowledgement(unsigned_resume_ack)==signed_resume_ack);
        resume_probe=*signed_resume_ack;resume_probe_expected=true;const auto probes_before=resume_probes;
        check(exchange("context").reply->payload.status==200);
        resume_probe_expected=false;(*resume_probe)["proof"]="xmj5DjfS6hTgs4wuAIWHwrKYXs25rjGsJ97pRqto45Y";
        check(exchange("context").reply->payload.status==200); // A fresh pair's key cannot forge the old outcome.
        resume_probe=*signed_resume_ack;(*resume_probe)["outcome"]="resumed";
        check(exchange("context").reply->payload.status==200);
        resume_probe=*signed_resume_ack;(*resume_probe)["controller_id"]=id('e');
        check(exchange("context").reply->payload.status==200);
        check(resume_probes==probes_before+4);resume_probe.reset();
        const auto new_context=exchange("context");check(new_context.reply&&new_context.reply->payload.status==200);
        const auto rebound=Json::parse(new_context.reply->payload.body);
        check(rebound["binding"]["connection_generation"]!=bound["binding"]["connection_generation"]&&rebound["binding"]["profile_generation"]=="1");
        save["binding"]=rebound["binding"];save["operation_id"]=id('6');
        const auto accepted_new=exchange("settings","PUT",save,"\""+fresh["revision"].get<std::string>()+"\"");
        check(accepted_new.reply&&accepted_new.reply->payload.status==200&&store.receipt(id('d'),"3",id('6')));
        const auto read_for_review=exchange("settings");check(read_for_review.reply&&read_for_review.reply->readback_revision.size()==64);
        const auto review_settings=Json::parse(read_for_review.reply->payload.body);
        Json review{{"contract","Wsprry-Control/1"},{"schema","target-operation-review"},{"binding",rebound["binding"]},
            {"operation_id",id('8')},{"readback_revision",read_for_review.reply->readback_revision}};
        const auto unchanged=store.settings();const auto authority_before=store.recurrence_authority();
        auto malformed=review;malformed["extra"]=true;
        const auto invalid_review=exchange("operations/"+id('8')+"/review","POST",malformed);
        check(invalid_review.reply&&invalid_review.reply->payload.status==400);
        check(exchange("operations/"+id('8')+"/review","POST",review).reply->payload.status==409);
        auto reviewed_read=exchange("settings");review["readback_revision"]=reviewed_read.reply->readback_revision;
        const auto later_read=exchange("settings");
        check(later_read.reply->readback_revision!=reviewed_read.reply->readback_revision);
        check(exchange("operations/"+id('8')+"/review","POST",review).reply->payload.status==409);
        reviewed_read=exchange("settings");review["readback_revision"]=reviewed_read.reply->readback_revision;
        auto wrong_id=review;wrong_id["operation_id"]=id('7');
        check(exchange("operations/"+id('8')+"/review","POST",wrong_id).reply->payload.status==400);
        reviewed_read=exchange("settings");review["readback_revision"]=reviewed_read.reply->readback_revision;
        const auto reviewed=exchange("operations/"+id('8')+"/review","POST",review);
        check(reviewed.reply&&reviewed.reply->payload.status==200);
        const auto acknowledgement=Json::parse(reviewed.reply->payload.body);
        check(valid_controller_document(acknowledgement,"target-operation-reviewed")&&acknowledgement["operation_id"]==id('8')&&
            acknowledgement["readback_revision"]==review["readback_revision"]&&acknowledgement["binding"]["connection_generation"]!=rebound["binding"]["connection_generation"]);
        check(store.settings()==unchanged&&store.recurrence_authority()==authority_before&&!store.receipt(id('d'),"3",id('8')));
        check(exchange("operations/"+id('8')+"/review","POST",review).reply->payload.status==409);
        // A queued save with the reviewed connection cannot execute afterward,
        // even if its configuration revision and current TLS key remain valid.
        save["operation_id"]=id('7');save["expected_revision"]=review_settings["revision"];save["candidate"]=review_settings["saved"];
        save["candidate"]["configuration"]["CW"]["Message"]="DELAYED AFTER REVIEW";
        const auto delayed=exchange("settings","PUT",save,read_for_review.reply->payload.etag);
        check(delayed.reply&&delayed.reply->payload.status==409&&Json::parse(delayed.reply->payload.body)["error"]["code"]=="connection_changed"&&
            store.settings()==unchanged&&!store.receipt(id('d'),"3",id('7')));
        const auto expiring=exchange("settings");review["binding"]=Json::parse(expiring.reply->payload.body)["binding"];
        review["readback_revision"]=expiring.reply->readback_revision;
        std::this_thread::sleep_for(std::chrono::seconds(31));
        check(exchange("operations/"+id('8')+"/review","POST",review).reply->payload.status==409&&store.settings()==unchanged);
        retirement_keys();
    }
    server.stop();paired->finish_service();
    std::cout<<"PASS actual TLS Pi service, typed authority, pair-scoped receipts, renewal and final Off/revocation refusal: "<<checks.load()<<" checks\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
