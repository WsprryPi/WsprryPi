// SPDX-License-Identifier: MIT
#include "diagnostic_export.hpp"
#include "advanced_settings.hpp"
#include "local_transport.hpp"
#include "runtime_options.hpp"
#include "web_presence.hpp"
#include "wire_bytes.hpp"
#include "application_recovery/restore_hold.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/persistent_configuration.hpp"
#include "config_handler.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "version.hpp"
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sqlite3.h>
#include <string_view>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
constexpr std::size_t maximum=2*SqliteStateStore::maximum_record_bytes;
void require(bool ok){if(!ok)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
bool hash(const Json &value){return value.is_string()&&value.get_ref<const std::string &>().size()==64&&value.get_ref<const std::string &>().find_first_not_of("0123456789abcdef")==std::string::npos;}
Json unavailable(const char *reason){return {{"schema","diagnostic-settings/1"},{"status","unavailable"},{"reason",reason}};}
}
Json diagnostic_settings_export(std::shared_ptr<SqliteStateStore> database,const std::string &boot){
    require(database&&database==application_database());
    const auto listeners=web_service_listeners();
    std::lock_guard lock(config_update_mutex());
    Json result{{"schema","diagnostic-settings/1"},{"status","unavailable"},{"role",database->role()==StateRole::Pi?"pi":"controller"},
        {"backend",{{"version",get_exe_version()},{"branch",get_exe_branch()},{"source_revision",get_exe_commit()},{"dirty",get_exe_build_dirty()},{"boot",boot}}},
        {"storage",{{"format","application-state/1"},{"schema_version",1},{"sqlite_version",sqlite3_libversion()},{"held",database->held()},
            {"namespace",database->directory()+"/configuration"}}},
        {"web_service",{{"http_port",listeners.http?Json(*listeners.http):Json(nullptr)},
            {"websocket_port",listeners.websocket?Json(*listeners.websocket):Json(nullptr)}}},
        {"restore_hold",application_restore_hold_view()}};
    if(database->held()){result["reason"]="storage_held";return result;}
    try{
        const auto record=database->record(StateRecord::Configuration);
        if(!record.bytes||!record.record_revision){result["reason"]="saved_settings_unavailable";return result;}
        // Decode and re-encode the closed supported settings model. Never dump
        // SQL rows, opaque owner records, source copies, journals, or key stores.
        const auto saved=decode_persistent_configuration(*record.bytes,database->role());
        result["saved_settings"]=Json::parse(encode_persistent_configuration(saved.configuration,saved.private_settings,saved.role));
        result["advanced_saved"]=advanced_values(saved);
        result["storage"]["revision"]=record.revision;result["storage"]["settings_revision"]=*record.record_revision;
        result["status"]="available";
    }catch(const StateStorageError &error){
        result["reason"]=error.code()==StateStorageError::Code::Unsupported?"unsupported_settings_schema":"saved_settings_invalid_or_unavailable";
    }catch(...){result["reason"]="saved_settings_invalid_or_unavailable";}
    result["storage"]["held"]=database->held();
    return result;
}
Json diagnostic_settings_chunk(std::shared_ptr<SqliteStateStore> database,const std::string &boot,const Json &request){
    require(request.is_object()&&request.size()==2&&request.contains("offset")&&request["offset"].is_string()&&request.contains("sha256"));
    const auto offset=wire_offset(request["offset"].get<std::string>());require(offset<=maximum&&(offset==0?request["sha256"].is_null():hash(request["sha256"])));
    const auto bytes=diagnostic_settings_export(database,boot).dump();require(bytes.size()<=maximum&&offset<bytes.size());
    const auto digest=PiManagementJournal::digest(bytes);
    if(offset&&request["sha256"]!=digest)throw LocalCommandRefused(LocalRefusal::RevisionConflict);
    const auto chunk=bytes.substr(static_cast<std::size_t>(offset),16384);
    return {{"schema","diagnostic-settings-chunk/1"},{"offset",std::to_string(offset)},{"size",std::to_string(bytes.size())},
        {"sha256",digest},{"bytes",encode_wire_chunk(chunk)}};
}
std::optional<int> diagnostic_export_main(int argc,char *argv[]){
    bool selected=false;for(int index=1;index<argc;++index)if(std::string_view(argv[index])=="--diagnostic-export")selected=true;if(!selected)return {};
    try{
        std::optional<std::string> runtime;bool seen=false;
        for(int index=1;index<argc;++index){const std::string option=argv[index];
            if(option=="--diagnostic-export"&&!seen)seen=true;
            else if(option=="--runtime-directory"&&!runtime&&index+1<argc)runtime=argv[++index];
            else throw LocalCommandRefused(LocalRefusal::InvalidRequest);
        }
        if(runtime)require(std::filesystem::path(*runtime).is_absolute()&&runtime->find('\0')==std::string::npos);
        std::string bytes,digest;std::uint64_t size{};
        do{
            const auto response=local_control_request(administrator_control_options(runtime),{LocalOperation::DiagnosticRead,
                {{"offset",std::to_string(bytes.size())},{"sha256",bytes.empty()?Json(nullptr):Json(digest)}}});
            require(response.at("ok")==true);const auto &chunk=response.at("value");
            require(chunk.is_object()&&chunk.size()==5&&chunk.value("schema",Json(nullptr))=="diagnostic-settings-chunk/1"&&
                chunk.value("offset",Json(nullptr))==std::to_string(bytes.size())&&chunk.contains("size")&&chunk["size"].is_string()&&
                chunk.contains("sha256")&&hash(chunk["sha256"])&&chunk.contains("bytes")&&chunk["bytes"].is_string());
            const auto total=wire_offset(chunk["size"].get<std::string>());require(total>0&&total<=maximum);
            if(bytes.empty()){size=total;digest=chunk["sha256"].get<std::string>();}
            require(size==total&&chunk["sha256"]==digest);
            const auto data=decode_wire_chunk(chunk["bytes"].get<std::string>());require(!data.empty()&&data.size()<=16384&&data.size()<=size-bytes.size());
            bytes+=data;
        }while(bytes.size()<size);
        require(PiManagementJournal::digest(bytes)==digest);const auto value=Json::parse(bytes);
        require(value.is_object()&&value.value("schema",Json(nullptr))=="diagnostic-settings/1"&&(value.value("status",Json(nullptr))=="available"||value.value("status",Json(nullptr))=="unavailable"));
        if(value["status"]=="available"){
            require(value.size()==9&&value.contains("role")&&(value["role"]=="pi"||value["role"]=="controller")&&
                value.contains("backend")&&value.contains("storage")&&value.contains("web_service")&&value.contains("restore_hold")&&
                value.contains("saved_settings")&&value.contains("advanced_saved"));
            const auto role=value["role"]=="pi"?StateRole::Pi:StateRole::Controller;
            const auto saved=decode_persistent_configuration(value["saved_settings"].dump(),role);
            require(value["advanced_saved"]==advanced_values(saved));
        }
        std::cout<<bytes<<'\n';return value["status"]=="available"?0:3;
    }catch(const LocalControlError &){std::cout<<unavailable("backend_or_local_control_unavailable").dump()<<'\n';return 3;}
    catch(...){std::cout<<unavailable("export_refused_or_changed_during_collection").dump()<<'\n';return 3;}
}
}
