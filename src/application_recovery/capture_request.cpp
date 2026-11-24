// SPDX-License-Identifier: MIT
#include "capture_request.hpp"
#include "snapshot.hpp"
#include "paths.hpp"
#include "job_lock.hpp"
#include "application_control/local_transport.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/companion_files.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "wtp_integration/browser_api.hpp"
#include <filesystem>
#include <mutex>
#ifdef _WIN32
#include "windows_file_io.hpp"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace wsprrypi {
namespace {
std::mutex capture_mutex;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
#ifdef _WIN32
std::filesystem::path disk_path(const std::string &path){return windows_file::path_from_utf8(path);}
std::string text(const std::filesystem::path &path){return recovery_windows::text(path);}
void private_directory(const std::filesystem::path &path){(void)recovery_windows::private_directory(path);}
#else
std::filesystem::path disk_path(const std::string &path){return path;}
std::string text(const std::filesystem::path &path){return path.string();}
void private_directory(const std::filesystem::path &path){
    struct stat info{};require(!::lstat(path.c_str(),&info)&&S_ISDIR(info.st_mode)&&info.st_uid==::geteuid()&&(info.st_mode&0777)==0700&&
        std::filesystem::canonical(path)==path);
}
#endif
}
nlohmann::json recovery_capture_request(const nlohmann::json &input,const std::string &boot){
    require(input.is_object()&&input.size()==1&&input.contains("job_id")&&input["job_id"].is_string());
    const auto id=input["job_id"].get<std::string>();require(id.size()==32&&id.find_first_not_of("0123456789abcdef")==std::string::npos);
    std::unique_lock capture(capture_mutex,std::try_to_lock);if(!capture.owns_lock())throw LocalCommandRefused(LocalRefusal::Busy);
    const auto paths=recovery_paths();const auto root=disk_path(paths.root),jobs=root/"jobs",job=jobs/id;
    private_directory(root);private_directory(jobs);private_directory(job);
    RecoveryJobLock job_lock(job,paths.backend_owner.has_value());
    const auto source=PiManagementJournal::read_private_record(text(job/"capture-admission.json"));require(source.has_value());
    const auto admission=strict_browser_json(source->bytes);
    const auto database=application_database();require(bool(database));
    require(admission.is_object()&&admission.size()==4&&admission.value("schema",nlohmann::json(nullptr))=="recovery-capture-admission/1"&&
        admission.value("job_id",nlohmann::json(nullptr))==id&&admission.value("backend_boot",nlohmann::json(nullptr))==boot&&
        admission.value("identity",nlohmann::json(nullptr))==database->identity());
    const auto result_path=text(job/"capture-result.json");
    if(const auto existing=PiManagementJournal::read_private_record(result_path)){
        const auto value=strict_browser_json(existing->bytes);
        require(value.is_object()&&value.size()==6&&value.value("schema",nlohmann::json(nullptr))=="recovery-capture-result/1"&&
            value.value("job_id",nlohmann::json(nullptr))==id&&value.value("backend_boot",nlohmann::json(nullptr))==boot&&
            value.contains("state")&&value.contains("inventory")&&value.contains("error"));
        require((value["state"]=="ready"&&value["inventory"].is_object()&&value["error"].is_null())||
            (value["state"]=="failed"&&value["inventory"].is_null()&&(value["error"]=="busy"||value["error"]=="storage_unavailable")));
        return {{"job_id",id},{"state",value.at("state")}};
    }
    require(!std::filesystem::exists(job/"snapshot"));
    prepare_companion_directory(text(job/"snapshot"));
    nlohmann::json result{{"schema","recovery-capture-result/1"},{"job_id",id},{"backend_boot",boot},{"state","failed"},{"inventory",nullptr},{"error",nullptr}};
    try{
        result["inventory"]=capture_application_recovery_snapshot(text(job/"snapshot"));result["state"]="ready";
    }catch(const LocalCommandRefused &error){result["error"]=error.reason()==LocalRefusal::Busy?"busy":"storage_unavailable";}
    catch(...){result["error"]="storage_unavailable";}
    const auto after=PiManagementJournal::read_private_record(text(job/"capture-admission.json"));
    require(after&&after->bytes==source->bytes&&after->signature==source->signature);
    (void)PiManagementJournal::publish_private_record(result_path,{},result.dump());
    return {{"job_id",id},{"state",result.at("state")}};
}
}
