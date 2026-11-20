// SPDX-License-Identifier: MIT
#include "application_control/diagnostic_export.hpp"
#include "application_control/advanced_runtime.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/persistent_configuration.hpp"
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace wsprrypi;
namespace {
using Json=nlohmann::json;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
}
int main(){try{
    char temporary[]="/tmp/diagnostic-export-XXXXXX";check(::mkdtemp(temporary),"Private diagnostic fixture");
    const auto root=std::filesystem::canonical(temporary);
    struct Remove {std::filesystem::path path;~Remove(){std::filesystem::remove_all(path);}} remove{root};
    std::ifstream input("wtp_integration/contract/control-controller-vectors.json");Json vectors;input>>vectors;
    Json configuration;for(const auto &item:vectors.at("cases"))if(item.at("name")=="Pi closed structural save")configuration=item.at("value").at("candidate").at("configuration");
    check(configuration.is_object(),"Actual current configuration vector");
    const auto long_message=std::string(32700,'A')+"☃";configuration["CW"]["Message"]=long_message;configuration["CW"]["Fade In Ms"]=27;
    PrivateConfiguration policy{true,SavedNetworkSafety::Enforced,true,false,true,37};
    check(!::mkdir((root/"state").c_str(),0700),"Private diagnostic database directory");
    auto database=std::make_shared<SqliteStateStore>((root/"state").string(),std::string(32,'a'),StateRole::Pi,StateOpen::CreateNew);
    const auto settings=encode_persistent_configuration(configuration,policy,StateRole::Pi);
    (void)database->commit(database->snapshot().revision,{{StateRecord::Configuration,settings},{StateRecord::Operations,"PRIVATE_OWNER_RECORD_MUST_NOT_BE_EXPORTED"}});
    ApplicationStateSelection selected(database);
    const auto before=database->snapshot();const auto exported=diagnostic_settings_export(database,std::string(32,'b'));
    check(exported.at("status")=="available"&&exported.at("saved_settings").at("configuration").at("CW").at("Message")==long_message,"Complete supported message is retained");
    check(exported.at("advanced_saved").at("CW.Fade In Ms")==27&&exported.at("saved_settings").at("private").at("startup_tx_iterations")==37,"Advanced and saved startup preferences are exported");
    check(exported.dump().size()>32768&&exported.dump().find("PRIVATE_OWNER_RECORD")==std::string::npos,"Large supported settings without opaque private records");
    check(database->snapshot()==before,"Diagnostic export is read-only");
    LocalTransportOptions options{(root/"control").string(),::geteuid(),static_cast<uid_t>(::geteuid()+1),::getegid()};
    std::atomic_bool change_after_first{};
    LocalControlServer server(options,std::string(32,'b'),[&](auto peer,const auto &request){
        auto response=backend_local_request(database,peer,request,std::string(32,'b'));
        if(change_after_first.exchange(false)){
            auto changed=configuration;changed["CW"]["Fade In Ms"]=28;
            (void)database->commit(database->snapshot().revision,{{StateRecord::Configuration,encode_persistent_configuration(changed,policy,StateRole::Pi)}});
        }
        return response;
    });server.start();
    const auto runtime=(root/"control").string();
    std::array<std::string,4> arguments{"diagnostic-fixture","--diagnostic-export","--runtime-directory",runtime};
    std::array<char *,5> argv{};for(std::size_t i=0;i<arguments.size();++i)argv[i]=arguments[i].data();
    const auto collect=[&]{std::ostringstream output;auto *original=std::cout.rdbuf(output.rdbuf());
        const auto result=diagnostic_export_main(4,argv.data());std::cout.rdbuf(original);return std::pair{result,output.str()};};
    const auto complete=collect();check(complete.first==0&&Json::parse(complete.second)==exported,"Actual private socket and chunked CLI preserve the complete export");
    change_after_first=true;const auto changed=collect();
    check(changed.first==3&&Json::parse(changed.second).at("status")=="unavailable"&&changed.second.find(long_message)==std::string::npos,"Changing settings refuse a mixed/partial export");
    server.stop();const auto missing=collect();
    check(missing.first==3&&Json::parse(missing.second).at("status")=="unavailable","Backend outage reports unavailable without storage fallback");
    std::cout<<"PASS supported/advanced diagnostic allowlist, no opaque records, large UTF-8 settings over real bounded IPC, read-only capture, revision-change refusal and unavailable backend\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
