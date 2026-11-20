// SPDX-License-Identifier: MIT
#include "application_storage/state_migration.hpp"
#include "application_storage/generation_selection.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/migration_source.hpp"
#include "application_recovery/restore_hold.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "scheduling_internal.hpp"
#include "scheduling.hpp"
#include "config_handler.hpp"
#include "wtp_integration/pi_pair_policy_admission.hpp"
#include "test_tone_request.hpp"
#include "software_child_process.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
const std::string self(32,'a'),job(32,'b');
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void put(const std::string &path,const Json &value){std::ofstream output(path);output<<value.dump();output.close();check(bool(output)&&!::chmod(path.c_str(),0600),"Private selector publication fixture");}
template<class F>void refuses(F action){bool refused=false;try{action();}catch(...){refused=true;}check(refused,"Expected generation/hold refusal");}
}
int main(int argc,char *argv[]){
    try{
        if(argc==5&&std::string(argv[1])=="--prepare-check"){
            auto database=open_application_state(argv[2],self,StateRole::Controller);ApplicationStateSelection selection(database);
            const auto phase=std::stoi(argv[4]);std::lock_guard admission(config_update_mutex());
            initialize_privileged_network_runtime(std::string("enforced"));
            {PiPairPolicyAdmission policy;check(policy.allowed(),"Ordinary enforced policy admits before maintenance");}
            prepare_application_restore_hold(database,argv[3],job,std::string(32,'c'),[&](RestoreHoldPoint point){
                {PiPairPolicyAdmission policy;check(!policy.allowed(),"Maintenance freezes actual pairing final-publication admission");}
                if(static_cast<int>(point)==phase)::_exit(60+phase);
            });
            return 2;
        }
        if(argc==3&&std::string(argv[1])=="--held-check"){
            auto database=open_application_state(argv[2],self,StateRole::Controller);
            ApplicationStateSelection selection(database);
            check(local_restore_output_held()&&restored_remote_authority_held(),"Both restore holds survive actual process startup");
            check(application_origin_namespace()==std::string(argv[2])+"/generation-1/configuration","Fresh installation origin survives generation replacement");
            ArgParserConfig candidate;candidate.transmit=true;candidate.use_ini=false;candidate.transmit_backend=TransmitBackendKind::SIMULATED;
            check(!runtime_transmit_preparation_enabled(candidate),"Saved/startup transmit intent cannot bypass the restore hold");
            const auto parsed=parse_test_tone_request(Json{{"frequency_source","custom_rf"},{"frequency_hz",14097100}});
            check(bool(parsed),"Valid synthetic tone request");
            const auto tone=start_test_tone(*parsed.request);
            check(!tone.started&&!tone.lifecycle.owned&&tone.message.find("destination review")!=std::string::npos,
                "Explicit tone refuses before output reservation while restored");
            check(application_restore_hold_view()["job_id"]==job,"Readback identifies the held restore");
            return 0;
        }
        check(argc==1,"Unsupported fixture arguments");
        char pattern[]="/tmp/restored-generation-XXXXXX";const auto temporary=::mkdtemp(pattern);check(temporary,"Private test root");
        const auto parent=std::filesystem::canonical(temporary).string(),root=parent+"/state";
        struct Remove{std::string path;~Remove(){std::filesystem::remove_all(path);}}remove{parent};
        for(unsigned phase=0;phase<4;++phase){
            const auto held_root=parent+"/hold-"+std::to_string(phase),job_root=parent+"/jobs-"+std::to_string(phase),job_directory=job_root+"/"+job;
            std::filesystem::create_directory(job_root);::chmod(job_root.c_str(),0700);std::filesystem::create_directory(job_directory);::chmod(job_directory.c_str(),0700);
            auto seed=initialize_application_state(held_root,self,StateRole::Controller,[&](const auto &name,const auto &installation){return initial_application_records(name,self,StateRole::Controller,installation);},validate_application_storage);
            const auto saved=seed->snapshot();seed.reset();
            const auto child=testing::launch_software_child(argv[0],{"--prepare-check",held_root,job_directory,std::to_string(phase)});int status{};
            check(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==60+static_cast<int>(phase),"Actual process loss at hold preparation boundary");
            refuses([&]{open_application_state(held_root,self,StateRole::Controller);});
            const auto recovery=testing::launch_software_child("/usr/bin/python3",{"-B","../scripts/tests/recovery_hold_recover_fixture.py",held_root,job_directory});
            check(testing::wait_software_child(recovery)==0,"Independent owner recovers held selector before backend admission");
            auto after=open_application_state(held_root,self,StateRole::Controller);check(after->snapshot()==saved,"Hold preparation never changes settings/receipts");
            // Release the root before the actual subsequent process startup.
            after.reset();
            check(testing::wait_software_child(testing::launch_software_child(argv[0],{"--held-check",held_root}))==0,"Recovered hold gates survive cold backend startup");
        }
        auto database=initialize_application_state(root,self,StateRole::Controller,[&](const auto &name,const auto &installation){
            return initial_application_records(name,self,StateRole::Controller,installation);
        },validate_application_storage);
        const auto initial=database->record(StateRecord::Initialization).bytes;check(initial.has_value(),"Immutable initialization exists");
        const auto before=database->snapshot();check(!::mkdir((root+"/generation-2").c_str(),0700),"New private generation");
        database->snapshot_to(root+"/generation-2");database.reset();
        Json selector{{"schema","application-generation/1"},{"identity",self},{"role","controller"},{"generation","2"},
            {"initialization",Json::parse(*initial)},{"restore_job",job}};
        Json hold{{"schema","application-restore-hold/1"},{"identity",self},{"role","controller"},{"generation","2"},
            {"job_id",job},{"local_output",true},{"remote_authority",true},{"revision","1"},{"stage","review"}};
        put(root+"/authority.json",selector);
        refuses([&]{open_application_state(root,self,StateRole::Controller);});
        auto malformed=hold;malformed["local_output"]=1;put(root+"/restore-hold.json",malformed);
        refuses([&]{open_application_state(root,self,StateRole::Controller);});
        malformed=hold;malformed["job_id"]=std::string(32,'c');put(root+"/restore-hold.json",malformed);
        refuses([&]{open_application_state(root,self,StateRole::Controller);});
        put(root+"/restore-hold.json",hold);
        database=open_application_state(root,self,StateRole::Controller);
        check(database->directory()==root+"/generation-2"&&database->snapshot()==before,"Only the selected complete generation opens");
        database.reset();
        refuses([&]{open_application_state(root,std::string(32,'c'),StateRole::Controller);});
        refuses([&]{open_application_state(root,self,StateRole::Pi);});
        for(const auto &field:std::vector<std::string>{"generation","identity","schema","unexpected"}){
            auto bad=selector;bad[field]="../generation-1";put(root+"/authority.json",bad);
            refuses([&]{open_application_state(root,self,StateRole::Controller);});
        }
        put(root+"/authority.json",selector);
        for(unsigned attempt=0;attempt<2;++attempt){
            const auto child=testing::launch_software_child(argv[0],{"--held-check",root});int status{};
            check(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"Actual repeated child startup preserves output and remote holds");
        }
        std::filesystem::remove_all(root+"/generation-2");
        refuses([&]{open_application_state(root,self,StateRole::Controller);});
        check(std::filesystem::exists(root+"/generation-1/state.sqlite3"),"Missing selected generation cannot adopt the prior generation implicitly");
        std::cout<<"PASS four actual hold-preparation crash boundaries, independent recovery before startup, unchanged settings/receipts, closed generation selection, immutable initialization, restart holds, no automatic output/tone admission and no older-generation fallback\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
