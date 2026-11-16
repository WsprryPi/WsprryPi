// SPDX-License-Identifier: MIT
#include "arg_parser.hpp"
#include "config_handler.hpp"
#include "config_handler_serialization.hpp"
#include "scheduling.hpp"
#include "wtp_pi_control.hpp"
#include "wtp_integration/pi_managed_startup.hpp"
#include "wtp_integration/pi_configuration_recovery.hpp"
#include "wtp_integration/pi_recovery_service.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <future>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){bool denied=false;try{action();}catch(...){denied=true;}check(denied,"Expected fail-closed startup refusal");}
std::string bytes(const std::string &path){std::ifstream input(path,std::ios::binary);return {(std::istreambuf_iterator<char>(input)),{}};}
void put(const std::string &path,const std::string &value){std::ofstream output(path,std::ios::binary);output<<value;output.close();check(bool(output)&&!chmod(path.c_str(),0600),"Owned fixture after-image");}
void fixture(const std::string &ini){
    init_default_config();set_scheduler_execution_suppressed_for_test(true);set_patch_all_from_web_runtime_apply_suppressed_for_test(true);
    config.use_ini=true;config.ini_filename=ini;config.transmit=false;config.simulated_backend_override=true;config.transmit_backend=TransmitBackendKind::SIMULATED;
    config.callsign="AA0NT";config.grid_square="EM18";config.power_dbm=20;config.frequencies="20m";
    config.wspr.callsign=config.callsign;config.wspr.grid_square=config.grid_square;
    config.wspr.power_dbm=config.power_dbm;config.wspr.frequencies=config.frequencies;
    config.wtp.transport="network_plain";config.wtp.hostname="127.0.0.1";config.wtp.device_id=std::string(32,'b');config.wtp.tcp_port=31417;
    resolve_backend_specific_config(config);config_to_json();
    jConfig["Operation"]["Transmit Backend"]="wtp";
    put(ini,"; startup fixture\n[Security]\nPrivileged Network Safety = enforced\n");
    iniFile.set_filename(ini);iniFile.set_raw_passthrough_keys({{"Security","Privileged Network Safety"}});json_to_ini();
}
}
int main(int argc,char **argv){
 try{
    if(argc!=2)return 2;
    std::ifstream vector_file(argv[1]);nlohmann::json vectors,recovery_command;vector_file>>vectors;
    for(const auto &entry:vectors["cases"])if(entry["name"]=="P209 complete recovery save")recovery_command=entry["value"];
    check(recovery_command.is_object(),"Current authoritative recovery command required");
    char pattern[]="/tmp/pi-managed-startup-XXXXXX";const auto temporary=mkdtemp(pattern);check(temporary,"Private startup fixture");const std::string root=temporary;
    struct Remove{std::string root;~Remove(){std::filesystem::remove_all(root);}} remove{root};
    const auto ini=root+"/settings.ini";const std::string self(32,'a');fixture(ini);
    std::string recovered_ini;
    {
        PiManagedStartup owner(ini,self);owner.prepare();
        check(!owner.admission_ready()&&!wtp_pi_begin_scheduled()&&!wtp_pi_begin_test_tone()&&!wtp_pi_begin_configuration(),
            "All local admission is closed before configuration validation and native activation");
        refuses([&]{validate_config_ini_input(ini+".other");});
        check(!std::filesystem::exists(ini+".managed-settings.json"),"Pre-parse recovery does not initialize absent history");
        refuses([&]{owner.activate([](const auto &action){action();});});
        std::string error;check(load_json(ini,&error,nullptr),"Owning parser validates the initial INI");
        check(owner.store().initialized()&&!owner.admission_ready(),"Validated settings alone cannot activate output");
        check(!owner.draft(),"Initial durable publication retires the parser's earlier file observation");
        const auto initial_generation=owner.store().settings()["configuration_generation"];
        check(load_json(ini,&error,nullptr)&&owner.store().settings()["configuration_generation"]==initial_generation,
            "Saved draft refresh observes the published file without another transaction");
        const auto initial_draft=owner.draft();
        check(initial_draft&&initial_draft->source["WSPR"]["Call Sign"]=="AA0NT"&&owner.configuration_validated(),
            "Read-only saved draft is captured by the owning parser independently of native readiness");
        const auto profiles=owner.catalog().snapshot(config.wtp).first;
        check(profiles["profiles"].size()==1&&profiles["active_unmatched"]==false,"First transaction preserves the existing selected WTP profile");
        unsigned reservations=0;bool permit=true;
        owner.activate([&](const auto &action){++reservations;if(!permit)throw std::runtime_error("native authority held");action();});
        check(owner.admission_ready()&&wtp_pi_begin_scheduled(),"Explicit native activation opens the separate readiness gate");
        std::promise<void> acquired,released;auto release=released.get_future();
        std::thread reader([&]{std::lock_guard lock(config_update_mutex());acquired.set_value();release.wait();});
        acquired.get_future().wait();const bool while_reading=owner.admission_ready();
        released.set_value();reader.join();
        check(while_reading,"An ordinary configuration reader cannot revoke verified output admission through mutex contention");
        const auto sentinel=root+"/unrelated";put(sentinel,"unrelated bytes");
        check(!symlink(sentinel.c_str(),(ini+".tmp").c_str()),"Predictable temporary symlink fixture");
        patch_all_from_web({{"CW",{{"Message","STARTUP OWNER"}}}});
        check(bytes(sentinel)=="unrelated bytes"&&std::filesystem::is_symlink(ini+".tmp")&&!std::filesystem::is_symlink(ini),
            "Managed save bypasses the predictable temporary symlink without touching unrelated bytes");
        check(!unlink((ini+".tmp").c_str()),"Remove temporary fixture link");
        const auto saved_bytes=bytes(ini);iniFile.set_string_value("CW","Message","UNCOORDINATED");
        const auto pending_ini=iniFile.getData();
        refuses([&]{iniFile.save();});
        check(bytes(ini)==saved_bytes&&iniFile.hasPendingChanges()&&iniFile.getData()==pending_ini,
            "Uncoordinated component save refuses without discarding pending edits or writing files");
        iniFile.load();
        check(!mkfifo((ini+".tmp").c_str(),0600),"Predictable temporary FIFO fixture");
        patch_all_from_web({{"CW",{{"Message","FIFO SAFE"}}}});
        struct stat temporary_info{};
        check(!lstat((ini+".tmp").c_str(),&temporary_info)&&S_ISFIFO(temporary_info.st_mode)&&bytes(sentinel)=="unrelated bytes",
            "Managed save does not open or replace the predictable FIFO");
        check(!unlink((ini+".tmp").c_str())&&!link(sentinel.c_str(),(ini+".tmp").c_str()),"Predictable temporary hardlink fixture");
        patch_all_from_web({{"CW",{{"Message","STARTUP OWNER"}}}});
        check(bytes(sentinel)=="unrelated bytes"&&!lstat((ini+".tmp").c_str(),&temporary_info)&&temporary_info.st_nlink==2,
            "Managed save does not truncate or replace the predictable hardlink");
        check(!unlink((ini+".tmp").c_str()),"Remove temporary hardlink fixture");
        const auto saved=owner.store().settings();
        check(reservations==3&&saved["configuration"]["CW"]["Message"]=="STARTUP OWNER","Legacy writer uses the admitted complete transaction owner");
        check(load_json(ini,&error,nullptr)&&owner.store().settings()==saved&&reservations==3,"Own monitor reload is observation, not another write");
        permit=false;const auto before=bytes(ini);
        refuses([&]{patch_all_from_web({{"CW",{{"Message","DENIED"}}}});});
        check(bytes(ini)==before&&owner.store().settings()==saved,"Native gate refusal precedes all persistence");
        permit=true;put(ini+".stock",before);persist_stock_configuration(true);
        check(reservations==5&&owner.store().settings()["configuration_generation"]=="5","Stock transaction enters the same native admission gate");
        auto pending=jConfig;pending["CW"]["Message"]="RECOVERED BEFORE PARSE";
        recovered_ini=serialize_config_ini_projection_for_management(pending);
        refuses([&]{owner.store().persist(pending,[&]{put(ini,recovered_ini);throw std::runtime_error("interrupted publication");});});
        check(!owner.admission_ready()&&!wtp_pi_begin_test_tone(),"Uncertain committed state closes admission immediately");
    }
    check(!wtp_pi_begin_scheduled()&&!wtp_pi_begin_test_tone(),"Owner destruction cannot reopen an unguarded output lane");
    {
        PiManagedStartup owner(ini,self);owner.prepare();
        check(bytes(ini)==recovered_ini&&!owner.admission_ready(),"Complete WAL recovery precedes parsing and never opens admission");
        iniFile.set_filename(ini);std::string error;check(load_json(ini,&error,nullptr),"Recovered INI passes the owning parser");
        owner.activate([](const auto &action){action();});check(owner.admission_ready(),"Recovered and validated authority can be explicitly activated");
        put(ini+".replacement",bytes(ini));check(!rename((ini+".replacement").c_str(),ini.c_str()),"Equal-byte external replacement");
        check(!wtp_pi_begin_scheduled()&&!wtp_pi_begin_test_tone()&&!wtp_pi_begin_configuration(),"External replacement cannot bypass any new local admission");
    }
    const auto invalid=root+"/invalid.ini";fixture(invalid);
    auto invalid_json=jConfig;invalid_json["CW"]["Fade Shape"]="invalid";put(invalid,serialize_config_ini_projection_for_management(invalid_json));
    iniFile.set_filename(invalid);
    {
        PiManagedStartup owner(invalid,self);owner.prepare();std::string error;
        check(!load_json(invalid,&error,nullptr),"Invalid initial INI is not a validated managed candidate");
        const auto invalid_draft=owner.draft();
        check(invalid_draft&&invalid_draft->source["CW"]["Fade Shape"]=="invalid"&&!owner.configuration_validated(),
            "Invalid saved values remain an unconfirmed parser draft, not normalized runtime defaults");
        refuses([&]{owner.activate([](const auto &action){action();});});
        check(!owner.admission_ready()&&!std::filesystem::exists(invalid+".managed-settings.json"),"Safe defaults cannot initialize managed history or activate output");
        auto readiness=PiHostReadiness::ConfigurationRecovery;
        refuses([&]{PiRecoveryService wrong_host(owner,std::string(32,'b'),std::string(32,'c'),[&]{return readiness;});});
        PiRecoveryService service(owner,self,std::string(32,'c'),[&]{return readiness;});
        PiRecoveryService unavailable(owner,self,std::string(32,'c'),[]()->PiHostReadiness{throw std::runtime_error("private startup detail");});
        const auto unavailable_status=unavailable.handle({"GET","/api/control/v1/host/readiness"},true);
        check(unavailable_status.status==503&&unavailable_status.body.find("private startup detail")==std::string::npos,
            "Unavailable host observation is a redacted service hold, not a candidate error or fabricated readiness");
        const std::string recovery_path="/api/control/v1/host/configuration-recovery";
        check(service.handle({"GET",recovery_path},false).status==403,"Recovery draft requires the protected local browser boundary");
        const auto recovery_view=service.handle({"GET",recovery_path},true);
        const auto view=nlohmann::json::parse(recovery_view.body);
        check(recovery_view.status==200&&view["writable"]==true&&view["draft"]["CW"]["Fade Shape"]["state"]=="unavailable"&&
            !owner.store().initialized(),"Reading a redacted recovery draft neither initializes history nor claims saved validity");
        check(view["draft"]["Si5351"]["I2C Address"]==invalid_json["Si5351"]["I2C Address"],
            "A valid saved hexadecimal address retains its current control representation in an unrelated invalid draft");
        auto bad_address=invalid_draft->source;bad_address["Si5351"]["I2C Address"]=112;
        check(pi_configuration_recovery_draft(bad_address)["Si5351"]["I2C Address"]["state"]=="unavailable",
            "Out-of-policy numeric addresses remain unavailable instead of being normalized into a valid candidate");
        auto request=recovery_command;request["binding"]["device_id"]=self;
        request["binding"]=view["binding"];request["capability_revision"]=view["capability_revision"];
        request["expected_revision"]=owner.store().recovery_revision();
        request["configuration"]=config_handler_serialization::saved_configuration_from_internal_json(invalid_json);
        request["configuration"]["CW"]["Fade Shape"]="none";
        request["configuration"]["CW"]["Message"]="EXPLICIT RECOVERY SAVE";
        for(const auto *key:{"TLS CA File","TLS Client Certificate","TLS Client Key"})request["configuration"]["WTP"][key]="";
        const PiManagedStore::Operation recovery{self,"1","/api/control/v1/host/configuration-recovery",request};
        const auto runtime_before=get_public_config_snapshot();const auto invalid_bytes=bytes(invalid);
        BrowserRequest browser{"PUT",recovery_path,request.dump(),recovery_view.etag,true};
        auto rejected=browser;rejected.mutation_context=false;
        check(service.handle(rejected,true).status==403,"Recovery mutation requires management CSRF context");
        rejected=browser;rejected.revision.clear();check(service.handle(rejected,true).status==428,"Recovery requires explicit If-Match");
        for(const auto *field:{"device_id","boot_id","profile_generation","connection_generation"}){
            auto stale=request;stale["binding"][field]=std::string(field==std::string("device_id")||field==std::string("boot_id")?32:1,'f');
            if(field==std::string("profile_generation")||field==std::string("connection_generation"))stale["binding"][field]="2";
            rejected=browser;rejected.body=stale.dump();check(service.handle(rejected,true).status==409,"Stale recovery binding refuses before persistence");
        }
        auto stale_capability=request;stale_capability["capability_revision"]=std::string(64,'f');
        rejected=browser;rejected.body=stale_capability.dump();check(service.handle(rejected,true).status==409,"Changed recovery descriptor refuses");
        rejected=browser;rejected.body=std::string(32769,'x');check(service.handle(rejected,true).status==413,"Recovery body cap precedes parsing");
        check(bytes(invalid)==invalid_bytes&&!owner.store().initialized()&&get_public_config_snapshot()==runtime_before,
            "All API authentication, context and bounds refusals preserve files and runtime");
        auto invalid_request=recovery;invalid_request.request["configuration"]["CW"]["Repeat Minutes"]=0;
        refuses([&]{owner.execute_recovery(invalid_request,[&]{persist_recovery_configuration(invalid_request.request["configuration"],invalid_draft->source);});});
        check(bytes(invalid)==invalid_bytes&&!owner.store().initialized()&&get_public_config_snapshot()==runtime_before,
            "Semantic recovery refusal preserves original INI, absent history, and runtime state");
        auto different_target=request["configuration"];different_target["WTP"]["Device ID"]=std::string(32,'f');
        refuses([&]{owner.execute_recovery(recovery,[&]{persist_recovery_configuration(different_target,invalid_draft->source);});});
        check(bytes(invalid)==invalid_bytes&&!owner.store().initialized(),
            "A validated but unauthorized complete candidate cannot seed initial managed history");
        const auto saved_response=service.handle(browser,true);
        check(saved_response.status==200,"Protected recovery service saves through the owning staged validator");
        const auto receipt=nlohmann::json::parse(saved_response.body);
        check(owner.store().initialized()&&owner.store().settings()["configuration_generation"]=="1"&&!owner.admission_ready(),
            "First explicit recovery save is one complete transaction, not a default/old-INI initialization followed by a second save");
        unsigned repeated=0;
        check(receipt["state"]=="committed"&&get_public_config_snapshot()==runtime_before&&
            owner.execute_recovery(recovery,[&]{++repeated;})==receipt&&repeated==0,
            "Staged recovery save does not publish runtime; exact replay never reruns validation or writes");
        check(!owner.draft(),"A saved-file change retires the old unconfirmed read context until parser readback");
        check(nlohmann::json::parse(service.handle({"GET",recovery_path},true).body)["writable"]==false,
            "A committed save cannot expose another editable draft before authoritative parser readback");
        refuses([&]{owner.activate([](const auto &action){action();});});
        check(load_json(invalid,&error,nullptr),"Explicit saved recovery candidate is reread through the owning INI parser");
        check(owner.catalog().snapshot(config.wtp).first["active_unmatched"]==false,
            "A rejected initial candidate cannot poison the selected-profile import of a later successful recovery");
        owner.activate([](const auto &action){action();});check(owner.admission_ready(),"Only valid saved readback permits subsequent native activation");
        readiness=PiHostReadiness::Ready;
        check(nlohmann::json::parse(service.handle({"GET","/api/control/v1/host/readiness"},true).body)["state"]=="ready"&&
            service.handle(browser,true).body==saved_response.body,"Normal readiness retains exact accepted recovery replay");
        check(owner.execute_recovery(recovery,[&]{++repeated;})==receipt&&repeated==0,
            "Accepted recovery replay survives normal readiness without dispatching another mutation");
    }
    const auto legacy=root+"/legacy.ini";fixture(legacy);
    iniFile.erase_value("GPIO","Use System Clock Frequency Estimate");iniFile.set_string_value("GPIO","Use NTP","true");iniFile.save();
    {
        PiManagedStartup owner(legacy,self);owner.prepare();std::string error;check(load_json(legacy,&error,nullptr),"Initial legacy migration passes the owning parser");
        check(owner.store().settings()["configuration_generation"]=="1"&&bytes(legacy).find("Use NTP")==std::string::npos,
            "Initial migration creates its exact normalized INI and history in one decision");
        owner.activate([](const auto &action){action();});check(owner.admission_ready(),"Migration receives the same post-write INI validation proof");
    }
    const auto partial=root+"/partial.ini";fixture(partial);
    const auto valid_stock=bytes(partial);put(partial+".stock",valid_stock);
    put(partial,"[CW]\nMessage = SAVED PARTIAL DRAFT\nFade Shape = invalid\nBase Frequency = nan\n[WSPR]\nCall Sign = 00123\n[GPIO]\nManual PPM = 1e-310\n[Band GPIO]\n20m = bad pin\n20m Active High = bad polarity\n[WTP]\nTLS Client Key = /private/device-key\n");
    iniFile.set_filename(partial);
    {
        PiManagedStartup owner(partial,self);owner.prepare();std::string error;
        check(!load_json(partial,&error,nullptr),"Missing required Operation section refuses ordinary startup");
        const auto saved=owner.draft();check(saved.has_value(),"Partial INI is observed before required-section or field validation");
        const auto redacted=pi_configuration_recovery_draft(*saved);
        check(redacted["Operation"]["Mode"]["state"]=="unavailable"&&redacted["Operation"]["Mode"]["reason"]=="missing"&&
            redacted["CW"]["Message"]=="SAVED PARTIAL DRAFT"&&redacted["CW"]["Fade Shape"]["state"]=="unavailable"&&
            redacted["GPIO"]["Manual PPM"].get<double>()==1e-310&&redacted["Band GPIO"]["20m"]["state"]=="unavailable"&&
            redacted["Band GPIO"]["20m"]["saved_text"].get<std::string>().find("bad polarity")!=std::string::npos&&
            redacted["CW"]["Base Frequency"]["saved_text"]=="\"nan\""&&
            redacted["WSPR"]["Call Sign"]["saved_text"]=="\"00123\""&&
            redacted.dump().find("/private/device-key")==std::string::npos,
            "Draft preserves known saved input, missing and malformed fields, and subnormal values without leaking private references or substituting defaults");
        check(!owner.configuration_validated()&&!owner.store().initialized()&&!owner.admission_ready(),
            "Read-only partial-draft observation grants no saved or output authority");
        PiRecoveryService service(owner,self,std::string(32,'d'),[]{return PiHostReadiness::ConfigurationRecovery;});
        const std::string path="/api/control/v1/host/configuration-recovery";
        const auto read=service.handle({"GET",path},true);const auto view=nlohmann::json::parse(read.body);
        auto action=recovery_command;action["schema"]="configuration-recovery-action";action.erase("configuration");
        action["binding"]=view["binding"];action["capability_revision"]=view["capability_revision"];
        action["expected_revision"]=view["revision"];action["operation_id"]=std::string(32,'e');action["action"]="repair";
        const auto before=bytes(partial);
        check(service.handle({"POST",path,action.dump(),read.etag,true},true).status==400&&bytes(partial)==before&&!owner.store().initialized(),
            "Repair cannot publish invalid preserved values or create authority from defaults");
        action["action"]="reset";action["operation_id"]=std::string(32,'f');
        const BrowserRequest reset{"POST",path,action.dump(),read.etag,true};
        const auto result=service.handle(reset,true);
        check(result.status==200&&bytes(partial)==valid_stock&&owner.store().settings()["configuration_generation"]=="1"&&
            !owner.configuration_validated()&&!owner.admission_ready()&&service.handle(reset,true).body==result.body,
            "Explicit Reset uses the validated exact stock transaction and durable replay without opening startup admission");
        put(partial+".replacement",valid_stock);check(!rename((partial+".replacement").c_str(),partial.c_str()),"Recovery external replacement fixture");
        const auto held_read=nlohmann::json::parse(service.handle({"GET","/api/control/v1/host/readiness"},true).body);
        check(held_read["state"]=="storage_recovery"&&held_read["recovery_editable"]==false&&service.handle(reset,true).status==503,
            "An initialized history mismatch becomes read-only storage recovery, never another Reset or invented readiness");
    }
    const auto held=root+"/held.ini";fixture(held);
    {
        PiManagedStartup owner(held,self);owner.prepare();std::string error;
        check(load_json(held,&error,nullptr),"Initialized storage-hold fixture");
    }
    put(held+".managed-settings.json","{corrupt initialized history");
    {
        const auto before=bytes(held);PiManagedStartup owner(held,self);
        refuses([&]{owner.prepare();});
        refuses([&]{validate_config_ini_input(held);});
        refuses([&]{patch_all_from_web({{"CW",{{"Message","BYPASS FAILED PREPARE"}}}});});
        refuses([&]{persist_stock_configuration(false);});
        check(bytes(held)==before&&!owner.admission_ready()&&!wtp_pi_begin_scheduled()&&!wtp_pi_begin_test_tone(),
            "Failed storage recovery retains refusing persistence owners as well as closed output admission");
    }
    const auto unsafe=root+"/fifo.ini";check(!mkfifo(unsafe.c_str(),0600),"Owned unsafe-input fixture");
    {
        PiManagedStartup owner(unsafe,self);refuses([&]{owner.prepare();});
        // Do not call the component: this guard is installed before its first
        // open in the production argument parser and must refuse immediately.
        refuses([&]{validate_config_ini_input(unsafe);});
        check(!owner.store().initialized()&&!owner.admission_ready()&&!std::filesystem::exists(unsafe+".managed-settings.json"),
            "Unsafe initial input refuses before any blocking component parse, authority creation, or output admission");
    }
    const auto reader=root+"/protected-reader.ini";fixture(reader);
    {
        PiManagedStartup owner(reader,self);owner.prepare();iniFile.set_filename(reader);
        validate_config_ini_input(reader);
        iniFile.set_string_value("CW","Message","PENDING READER DRAFT");
        const auto data=iniFile.getData();const auto layout=iniFile.serialize_with_data(data);
        const auto original=bytes(reader);
        check(!rename(reader.c_str(),(reader+".kept").c_str())&&!mkfifo(reader.c_str(),0600),"Replace checked input with FIFO");
        refuses([&]{iniFile.load();});
        check(iniFile.getData()==data&&iniFile.hasPendingChanges()&&iniFile.serialize_with_data(data)==layout,
            "Protected component read refuses post-check FIFO without blocking or losing pending work");
        check(!unlink(reader.c_str())&&!rename((reader+".kept").c_str(),reader.c_str()),"Restore owned reader input");
        const auto target=root+"/reader-target.ini";put(target,"PRIVATE TARGET MUST STAY UNREAD");
        check(!rename(reader.c_str(),(reader+".kept").c_str())&&!symlink(target.c_str(),reader.c_str()),"Replace checked input with symlink");
        refuses([&]{iniFile.load();});
        check(iniFile.getData()==data&&bytes(target)=="PRIVATE TARGET MUST STAY UNREAD","Component does not follow replacement symlink");
        check(!unlink(reader.c_str())&&!rename((reader+".kept").c_str(),reader.c_str()),"Restore symlink fixture");
        check(!chmod(reader.c_str(),0666),"Untrusted read input mode");refuses([&]{iniFile.load();});
        check(!chmod(reader.c_str(),0600),"Restore read input mode");iniFile.load();
        check(!symlink(target.c_str(),(reader+".stock").c_str()),"Untrusted stock symlink");
        refuses([&]{iniFile.serialize_from_stock("v",false);});
        check(bytes(reader)==original&&bytes(target)=="PRIVATE TARGET MUST STAY UNREAD","Stock reader does not follow links or mutate either input");
        check(!unlink((reader+".stock").c_str())&&!mkfifo((reader+".stock").c_str(),0600),"Untrusted stock FIFO");
        refuses([&]{iniFile.serialize_from_stock("v",true);});
        check(!unlink((reader+".stock").c_str()),"Remove owned stock FIFO");put(reader+".stock",std::string(1048577,'x'));
        refuses([&]{iniFile.serialize_from_stock("v",false);});
        refuses([&]{iniFile.set_filename(target);});
        check(!owner.store().initialized()&&!owner.admission_ready(),"Protected input reads grant neither managed history nor output admission");
    }
    {
        const auto stopped=root+"/stopping.ini";fixture(stopped);
        PiManagedStartup owner(stopped,self);owner.prepare();std::string error;
        check(load_json(stopped,&error,nullptr),"Valid stopping owner");owner.activate([](const auto &action){action();});
        check(owner.admission_ready(),"Stopping fixture starts with verified ownership");
        const auto original=bytes(stopped);
        const auto snapshot=get_public_config_snapshot();
        wtp_pi_management_stopping();clear_wtp_pi_control_hooks();
        check(!owner.admission_ready()&&!wtp_pi_begin_scheduled()&&!wtp_pi_begin_test_tone()&&!wtp_pi_begin_configuration(),
            "Endpoint teardown closes the durable owner before native hooks are cleared");
        refuses([&]{patch_all_from_web({{"CW",{{"Message","AFTER TEARDOWN"}}}});});
        check(bytes(stopped)==original&&get_public_config_snapshot()==snapshot,"A stopped owner cannot publish or persist another configuration");
    }
    check(!config.transmit,"No transmitter was enabled by the fixture");
    std::cout<<"PASS: pre-parse WAL recovery, initial semantic validation, selected-profile preservation, all legacy/stock writer gates, explicit readiness, refusal before persistence, external-file fences, and invalid-startup hold\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
