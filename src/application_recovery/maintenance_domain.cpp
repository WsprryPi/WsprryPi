// SPDX-License-Identifier: MIT
#include "maintenance_domain.hpp"
#include "private_json.hpp"
#include "backup_artifact.hpp"
#include "application_control/http_contract.hpp"
#include "application_control/wire_bytes.hpp"
#include "application_control/local_transport.hpp"
#include "wtp_integration/browser_api.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include <array>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
void error(httplib::Response &response,int status,const char *code){response.status=status;response.set_content(Json{{"error",code}}.dump(),"application/json");}

}
RecoveryMaintenanceDomain::RecoveryMaintenanceDomain(const RecoveryPaths &paths,RecoveryWorkerProcess &worker,RecoveryBrowserAdmission &admission)
    :paths_(paths),worker_(worker),admission_(admission){
    const auto handler=[this](const auto &request,auto &response){handle(request,response);};
    Get(R"(/api/recovery/.*)",handler);Post(R"(/api/recovery/.*)",handler);
    initialize_domain_capture();
}
void RecoveryMaintenanceDomain::handle(const httplib::Request &request,httplib::Response &response){
    response.set_header("Cache-Control","no-store");
    if(!admission_.permits(request)){error(response,403,"maintenance_admission_required");return;}
    const auto operation=browser_http_operation(request.method,request.path);
    if(!operation||!operation->name.starts_with("recovery.")||operation->name=="recovery.session") {error(response,404,"not_found");return;}
    try{
        const bool read=request.method=="GET"||request.method=="HEAD";
        require(!read||request.body.empty());
        Json command=read?Json::object():parse_recovery_json(request.body,operation->maximum_body);require(command.is_object());
        std::string id;
        constexpr std::string_view prefix="/api/recovery/jobs/";
        if(request.path.starts_with(prefix))id=request.path.substr(prefix.size(),32);
        if(operation->name=="recovery.download"){download(id,response);return;}
        require((operation->name=="recovery.review"||!command.contains("action"))&&!command.contains("job_id"));
        if(operation->name=="recovery.review"){
            command=Json{{"action","review"},{"review",read?Json{{"action","read"}}:command}};
        }else if(operation->name=="recovery.jobs"){
            if(read)command["action"]="list";
            else{
                require(command.contains("kind")&&command["kind"].is_string());const auto kind=command["kind"].get<std::string>();command.erase("kind");
                require(kind=="backup"||kind=="restore");command["action"]=kind=="backup"?"backup.create":"restore.upload.start";
            }
        }else{
            command["job_id"]=id;
            if(read)command["action"]="status";
            else{
                const auto action=request.path.substr(prefix.size()+32);
                if(action=="/chunk")command["action"]="restore.upload.chunk";
                else if(action=="/finish")command["action"]="restore.upload.finish";
                else if(action=="/validate")command["action"]="restore.validate";
                else if(action=="/confirm")command["action"]="restore.confirm";
                else if(action=="/resume")command["action"]="restore.resume";
                else if(action=="/cancel")command["action"]="cancel";
                else throw LocalCommandRefused(LocalRefusal::InvalidRequest);
            }
        }
        const auto result=worker_.request(command);
        if(result["ok"]!=true){const auto code=result["error"].get<std::string>();
            error(response,code=="busy"?409:code=="invalid_request"?400:503,code.c_str());return;}
        response.status=200;response.set_content(result["value"].dump(),"application/json");
    }catch(const LocalCommandRefused &){error(response,400,"invalid_request");}
    catch(const LocalControlError &){error(response,503,"outcome_unknown");}
    catch(...){error(response,503,"maintenance_unavailable");}
}
void RecoveryMaintenanceDomain::download(const std::string &id,httplib::Response &response){
    auto file=std::make_shared<RecoveryBackupArtifact>(paths_,id);
    response.status=200;response.set_header("Content-Disposition","attachment; filename=\"wsprrypi-backup-"+id+".age\"");
    response.set_header("X-WsprryPi-Archive-SHA256",file->sha256());
    response.set_content_provider(static_cast<std::size_t>(file->size()),"application/octet-stream",[file](std::size_t offset,std::size_t length,httplib::DataSink &sink){
        try{const auto bytes=file->read(offset,std::min<std::size_t>(65536,length));return !bytes.empty()&&sink.write(bytes.data(),bytes.size());}
        catch(...){return false;}
    });
}
}
