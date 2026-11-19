// SPDX-License-Identifier: MIT
// Host-only pipe fixture for the production independent settings transport.
// There is deliberately no WTP application, selected output, or CLAIM here.
#include "wtp_integration/pico_management_context.hpp"
#include <filesystem>
#include <unistd.h>
#include "wtp_integration/browser_api.hpp"
#include "wtp_runtime_bridge.hpp"
#include <chrono>
#include <iostream>
namespace {
struct Resolver final : wsprrypi::TlsResolver {
    bool begin(const std::string &,unsigned) override {return true;}
    auto poll() -> std::optional<std::vector<std::string>> override {return std::vector<std::string>{"127.0.0.1"};}
    void cancel() noexcept override {}
};
std::uint64_t now() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    try {
        const std::string directory=argv[1];
        wsprrypi::TlsSelection selection{"wsprrypico-"+std::string(32,'a')+".local","",directory+"/client-ca.crt",directory+"/client.crt",directory+"/client.key",18443};
        auto credentials=std::make_shared<wsprrypi::TlsCredentials>(selection);
        wsprrypi::TlsStream tls(now,wsprrypi::TlsStream::Access::LoopbackTest,std::make_unique<Resolver>());
        WtpSettings settings;settings.device_id=std::string(32,'a');settings.transport="network";
        settings.hostname=selection.host;settings.tcp_port=selection.port;settings.tls_ca=selection.ca_file;
        settings.tls_certificate=selection.certificate_file;settings.tls_key=selection.key_file;
        char pattern[]="/tmp/pico-management-pair-XXXXXX";const auto temporary=mkdtemp(pattern);
        if(!temporary)throw std::runtime_error("Private fixture directory unavailable");
        struct Remove {std::string path;~Remove(){std::filesystem::remove_all(path);}} remove{temporary};
        bool drop=false,fail_receipt_storage=false;unsigned mutations=0;
        wsprrypi::PicoManagementContext target(settings,std::string(32,'c'),std::string(temporary)+"/journal","1","1",{[&](const auto &path,const auto &verb,const auto &body,const auto &revision) {
            const auto response=wsprrypi::pico_http_request(tls,selection,credentials,now,path,verb,body,revision);
            if(verb!="GET"&&fail_receipt_storage){fail_receipt_storage=false;std::filesystem::permissions(std::string(temporary)+"/journal/"+settings.device_id+".json",
                std::filesystem::perms::owner_read|std::filesystem::perms::owner_write|std::filesystem::perms::group_read);}
            if(verb!="GET") {++mutations;if(drop){drop=false;return wsprrypi::PicoHttpResponse{503,R"({"error":{"code":"management_outcome_unknown"}})",{}};}}
            return response;
        },{},{}},wsprrypi::PicoManagementHistoryAuthority::isolated());
        const auto connected=target.connect("engineering_mtls");
        if(connected.status!=200)throw std::runtime_error("Independent negotiation refused: "+connected.body);
        std::cout<<"READY"<<std::endl;
        std::string line;
        while(std::getline(std::cin,line)) {
            const auto command=wsprrypi::strict_browser_json(line);
            if(command.value("control","")=="drop_reply") {drop=true;std::cout<<"{}"<<std::endl;continue;}
            if(command.value("control","")=="fail_receipt_storage") {fail_receipt_storage=true;std::cout<<"{}"<<std::endl;continue;}
            if(command.value("control","")=="repair_receipt_storage") {
                std::filesystem::permissions(std::string(temporary)+"/journal/"+settings.device_id+".json",std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);
                std::cout<<"{}"<<std::endl;continue;
            }
            if(command.value("control","")=="seed_terminal_capacity") {
                // Typed fixture setup only; no browser or runtime selector can
                // create these historical controller receipts.
                wsprrypi::PicoManagementJournal history(std::string(temporary)+"/journal",std::string(32,'c'),settings.device_id);
                for(unsigned i=0;i<256;++i) {
                    const auto digest=wsprrypi::PicoManagementJournal::digest("seed-"+std::to_string(i));
                    const auto id=digest.substr(0,32);
                    history.reserve(std::string(32,'c'),"/api/control/v1/targets/"+settings.device_id+"/pico/configuration",
                        "POST",target.binding(),"1",id,digest);
                    history.finish(id,{200,R"({"contract":"Wsprry-Control/1","saved":true})",{}});
                }
                std::cout<<"{}"<<std::endl;continue;
            }
            wsprrypi::PicoHttpResponse response;
            if(command.value("control","")=="reconcile")response=target.reconcile(command.at("operation_id"),command.at("readback_revision"));
            else if(command.value("control","")=="receipt") {
                if(!target.authorized())response={401,R"({"error":{"code":"unauthenticated"}})",{}};
                else {const auto receipt=target.receipt(command.at("operation_id"));response=receipt.is_null()?wsprrypi::PicoHttpResponse{404,R"({"error":{"code":"operation_unknown"}})",{}}:wsprrypi::PicoHttpResponse{200,receipt.dump(),{}};}
            }
            else response=target.request({command.at("method"),"/api/control/v1/targets/"+settings.device_id+"/pico/"+command.at("resource").get<std::string>(),
                command.value("body",""),command.value("revision",""),true,command.value("member_context","")},
                command.value("context",target.id()),command.value("operation_id",""));
            const auto recovery=target.recovery_hint();
            std::cout<<nlohmann::json{{"status",response.status},{"body",wsprrypi::strict_browser_json(response.body)},
                {"etag",response.etag},{"binding",target.binding()},{"context",target.id()},{"readback_revision",target.readback_revision()},{"recovery_operation",recovery.first},{"recovery_resource",recovery.second},{"mutations",mutations},{"execution_ready",wtp_runtime_ready()}}.dump()<<std::endl;
        }
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
