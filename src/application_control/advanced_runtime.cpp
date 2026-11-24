// SPDX-License-Identifier: MIT
#include "advanced_runtime.hpp"
#include "diagnostic_export.hpp"
#include "application_recovery/capture_request.hpp"
#include "application_recovery/validation_request.hpp"
#include "application_recovery/prepare_request.hpp"
#include "application_recovery/destination_review.hpp"
#include "wtp_integration/pi_management_journal.hpp"
#include "application_recovery/restore_hold.hpp"
#include <filesystem>
#ifdef _WIN32
#include "native_windows_file.hpp"
#else
#include <unistd.h>
#endif
#include "advanced_settings.hpp"
#include "event_feed.hpp"
#include "browser_sessions.hpp"
#include "http_sessions.hpp"
#include "web_presence.hpp"
#include "application_storage/application_state.hpp"
#include "application_storage/route_maintenance.hpp"
#include "config_handler.hpp"
#include "controller_runtime.hpp"
#include "pi_managed_host_runtime.hpp"
#include "web_server.hpp"
#include "web_socket.hpp"
#include "scheduling.hpp"
#include <charconv>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value,LocalRefusal reason=LocalRefusal::InvalidRequest){if(!value)throw LocalCommandRefused(reason);}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto *name:names)if(!value.contains(name))return false;
    return true;
}
bool revision(const Json &value){
    if(!value.is_string())return false;
    const auto &text=value.get_ref<const std::string &>();if(text.empty()||text.front()=='0')return false;
    std::uint64_t number{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&number>0;
}
std::string editing(std::shared_ptr<SqliteStateStore> database){
    if(exiting_wspr.load())return "stopping";
    if(database->role()==StateRole::Controller)return controller_only_ready()?"ready":"starting";
    if(!pi_managed_host_selected())return "ready";
    switch(pi_managed_host_state()){
        case PiHostReadiness::Starting:return "starting";
        case PiHostReadiness::Ready:return "ready";
        case PiHostReadiness::StorageRecovery:return "storage_recovery";
        case PiHostReadiness::OutputRecovery:return "output_recovery";
        case PiHostReadiness::ConfigurationRecovery:return database->record(StateRecord::ManagedSettings).bytes?"configuration_recovery":"setup_required";
    }
    return "storage_recovery";
}
}
Json advanced_settings_view(std::shared_ptr<SqliteStateStore> database){
    require(database&&application_database()==database,LocalRefusal::StorageUnavailable);
    // Observe listener state without reversing the listener/configuration lock
    // order. This is a status observation, not proof of physical output.
    const auto listeners=web_service_listeners();const auto http=listeners.http,websocket=listeners.websocket;
    std::lock_guard lock(config_update_mutex());
    const auto record=database->record(StateRecord::Configuration);
    require(record.bytes&&record.record_revision,LocalRefusal::StorageUnavailable);
    const auto saved=decode_persistent_configuration(*record.bytes,database->role());
    const auto values=advanced_values(saved);const auto state=editing(database);
    Json active{{"Meta.debug_logging",state=="ready"?Json(config.debug_logging):Json(nullptr)},{"Operation.Web Port",http?Json(*http):Json(nullptr)},
        {"Operation.Socket Port",websocket?Json(*websocket):Json(nullptr)}};
    if(database->role()==StateRole::Pi&&state=="ready"){
        active["Experimental.Allow Unqualified Frequency"]=config.allow_unqualified_frequency;
        active["Experimental.Allow Non-Amateur Frequency"]=config.allow_non_amateur_frequency;
        active["Si5351.TX Output"]="CLK"+std::to_string(config.si5351_tx_output);
        active["CW.Fade Shape"]=config.cw_fade_shape;active["CW.Fade In Ms"]=config.cw_fade_in_ms;
        active["CW.Fade Out Ms"]=config.cw_fade_out_ms;active["CW.Fade Slice Ms"]=config.cw_fade_slice_ms;
    }
    for(const auto &[name,value]:values.items()){(void)value;if(!active.contains(name))active[name]=nullptr;}
    Json settings=Json::object();
    for(const auto &[name,value]:values.items()){
        const auto &field=*advanced_field(name);const bool valid=valid_advanced_value(field,value);
        const auto current=active.at(name);const bool port=name=="Operation.Web Port"||name=="Operation.Socket Port";
        const auto activation=!valid?"invalid":value==current?"active":port?(current.is_null()?"inactive":"restart_required"):
            state=="ready"?"startup_override":state=="output_recovery"?"restart_required":"pending";
        // Invalid enum text is not a supported setting value. Bound its
        // diagnostic rendering without silently truncating or rewriting it.
        const auto displayed=value.is_string()&&value.get_ref<const std::string &>().size()>128?Json(nullptr):value;
        settings[name]={{"saved",displayed},{"active",current},{"valid",valid},{"activation",activation}};
    }
    return {{"schema","advanced-settings/1"},{"role",database->role()==StateRole::Pi?"pi":"controller"},
        {"revision",*record.record_revision},{"editing",state},{"settings",settings}};
}
Json backend_local_request(std::shared_ptr<SqliteStateStore> database,LocalPeer peer,const LocalRequest &request,const std::string &boot){
    require(database&&application_database()==database,LocalRefusal::StorageUnavailable);
    if(request.operation==LocalOperation::WebBootstrap){require(request.arguments.empty());return web_service_bootstrap(database,boot);}
    if(request.operation==LocalOperation::WebAttach||request.operation==LocalOperation::WebHeartbeat)return backend_web_presence(request);
    if(request.operation==LocalOperation::BrowserOpen||request.operation==LocalOperation::BrowserMessage||request.operation==LocalOperation::BrowserCheck)
        return backend_browser_request(request);
    if(request.operation==LocalOperation::HttpOpen||request.operation==LocalOperation::HttpBody||request.operation==LocalOperation::HttpDispatch||
        request.operation==LocalOperation::HttpRead||request.operation==LocalOperation::HttpComplete)return backend_http_request(request);
    if(request.operation==LocalOperation::EventsRead){
        require(fields(request.arguments,{"boot","after"})&&request.arguments["boot"].is_string()&&request.arguments["after"].is_string());
        const auto cursor=request.arguments["after"].get<std::string>();std::uint64_t after{};
        const auto parsed=std::from_chars(cursor.data(),cursor.data()+cursor.size(),after);
        require(!cursor.empty()&&(cursor.size()==1||cursor.front()!='0')&&parsed.ec==std::errc{}&&parsed.ptr==cursor.data()+cursor.size());
        const auto result=backend_events().read(after,1);
        const bool resync=request.arguments["boot"]!=boot||result.resync_required;
        Json events=Json::array();
        if(!resync)for(const auto &event:result.events)events.push_back({{"sequence",std::to_string(event.sequence)},{"message",event.message}});
        Json response{{"schema","local-events/1"},{"boot",boot},{"sequence",std::to_string(resync?result.head:result.sequence)},
            {"resync_required",resync},{"events",events}};
        if(response.dump().size()>120000){response["resync_required"]=true;response["sequence"]=std::to_string(result.head);response["events"]=Json::array();}
        return response;
    }
    if(request.operation==LocalOperation::ListenerRead){
        require(request.arguments.empty());
        std::lock_guard lock(config_update_mutex());
        return {{"schema","listener-bootstrap/1"},{"device_id",database->identity()},
            {"role",database->role()==StateRole::Pi?"pi":"controller"},
            {"web_port",config.web_port},{"socket_port",config.socket_port}};
    }
    require(peer==LocalPeer::Administrator);
    if(request.operation==LocalOperation::DiagnosticRead)return diagnostic_settings_chunk(database,boot,request.arguments);
    if(controller_only_mode()&&(request.operation==LocalOperation::RecoveryReview||request.operation==LocalOperation::RecoveryPrepare||
        request.operation==LocalOperation::RecoveryValidate||request.operation==LocalOperation::RecoveryCapture))
        require(controller_only_ready(),LocalRefusal::StorageUnavailable);
    if(request.operation==LocalOperation::RecoveryReview)return recovery_destination_review(request.arguments,boot);
    if(request.operation==LocalOperation::RecoveryPrepare)return recovery_prepare_request(request.arguments,boot);
    if(request.operation==LocalOperation::RecoveryValidate)return recovery_validation_request(request.arguments,boot);
    if(request.operation==LocalOperation::RecoveryCapture)return recovery_capture_request(request.arguments,boot);
    if(request.operation==LocalOperation::RecoveryObserve){
        require(request.arguments.empty());
        const auto active_listeners=web_service_listeners();
        const auto bootstrap=web_service_bootstrap(database,boot);
        const auto directory=std::filesystem::path(database->directory());const auto name=directory.filename().string();
        require(name.starts_with("generation-"),LocalRefusal::StorageUnavailable);
        const auto configuration=database->record(StateRecord::Configuration);require(configuration.bytes.has_value(),LocalRefusal::StorageUnavailable);
        return {{"schema","recovery-backend-observation/1"},{"identity",database->identity()},{"role",database->role()==StateRole::Pi?"pi":"controller"},
            {"boot",boot},{"pid",
#ifdef _WIN32
                ::GetCurrentProcessId()
#else
                ::getpid()
#endif
            },{"generation",name.substr(11)},{"restore_hold",application_restore_hold_view()},
            {"ready",database->role()==StateRole::Controller?controller_only_ready():pi_managed_host_selected()&&!pi_managed_host_held()},
            {"listeners",{{"web_port",bootstrap.at("web_port")},{"socket_port",bootstrap.at("socket_port")},
                {"http_enabled",bootstrap.at("http_enabled")},{"websocket_enabled",bootstrap.at("websocket_enabled")},
                {"http_bound",active_listeners.http?Json(*active_listeners.http):Json(nullptr)},
                {"websocket_bound",active_listeners.websocket?Json(*active_listeners.websocket):Json(nullptr)}}},
            {"storage_scope",PiManagementJournal::digest(directory.parent_path().string())},
            {"configuration_digest",PiManagementJournal::digest(*configuration.bytes)}};
    }
    if(request.operation==LocalOperation::RouteRead){
        require(request.arguments.empty()&&database->role()==StateRole::Pi);
        return route_maintenance_state(database);
    }
    if(request.operation==LocalOperation::SettingsRead){require(request.arguments.empty());return advanced_settings_view(database);}
    require(request.operation==LocalOperation::SettingsWrite&&fields(request.arguments,{"expected_revision","changes"})&&revision(request.arguments["expected_revision"]));
    require(!restore_maintenance_active(),LocalRefusal::Busy);
    const auto expected=request.arguments["expected_revision"].get<std::string>();
    if(database->role()==StateRole::Controller)persist_controller_advanced_settings(expected,request.arguments["changes"]);
    else persist_owned_advanced_settings(expected,request.arguments["changes"]);
    return advanced_settings_view(database);
}
}
