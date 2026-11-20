// SPDX-License-Identifier: MIT
#include "browser_sessions.hpp"
#include "application_recovery/restore_hold.hpp"
#include "config_handler.hpp"
#include "privileged_network_runtime.hpp"
#include "websocket_upgrade_guard.hpp"
#include "web_socket.hpp"
#include "scheduling.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <algorithm>
namespace wsprrypi {
namespace {
using Json=nlohmann::json;
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
bool fields(const Json &value,std::initializer_list<const char *> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto *name:names)if(!value.contains(name))return false;
    return true;
}
struct Session {
    std::mutex mutex;
    std::string upgrade,peer;
    std::uint64_t tone_generation{};
    bool closed{};
    WebSocketUpgradeGuardResult admission()const{return evaluate_websocket_upgrade(upgrade,peer,SupportRequestGuard::discover_local_networks(),active_privileged_network_mode());}
    bool authorized()const{return admission().decision==WebSocketUpgradeGuardDecision::allowed;}
};
struct Sessions {std::mutex mutex;std::map<std::string,std::shared_ptr<Session>> values;};
Sessions &sessions(){static Sessions value;return value;}
}
Json backend_browser_request(const LocalRequest &request){
    require(request.transport_connection.size()==32&&!exiting_wspr.load());
    auto &owner=sessions();
    if(request.operation==LocalOperation::BrowserOpen){
        require(fields(request.arguments,{"upgrade","peer"})&&request.arguments["upgrade"].is_string()&&request.arguments["peer"].is_string());
        auto session=std::make_shared<Session>();session->upgrade=request.arguments["upgrade"].get<std::string>();
        session->peer=request.arguments["peer"].get<std::string>();
        require(session->upgrade.size()<=4096&&session->peer.size()<=64);
        const auto admitted=session->admission();require(admitted.decision==WebSocketUpgradeGuardDecision::allowed);
        {std::lock_guard lock(config_update_mutex());require(config.enable_web);}
        std::lock_guard lock(owner.mutex);
        require(owner.values.size()<4&&!owner.values.contains(request.transport_connection));
        owner.values.emplace(request.transport_connection,std::move(session));
        return {{"schema","browser-session/1"},{"connected",true},{"key",admitted.key}};
    }
    std::shared_ptr<Session> session;
    {std::lock_guard lock(owner.mutex);const auto found=owner.values.find(request.transport_connection);require(found!=owner.values.end());session=found->second;}
    std::lock_guard lock(session->mutex);require(!session->closed&&session->authorized());
    if(request.operation==LocalOperation::BrowserCheck){require(request.arguments.empty());return {{"connected",true}};}
    require(request.operation==LocalOperation::BrowserMessage&&fields(request.arguments,{"message"})&&request.arguments["message"].is_string());
    const auto message=request.arguments["message"].get<std::string>();require(message.size()<=64*1024);
    // Validate shape before the existing dispatcher touches command fields.
    std::vector<std::set<std::string>> keys;
    Json command;
    try{command=Json::parse(message,[&](int depth,Json::parse_event_t event,Json &value){
        require(depth<=32);
        if(event==Json::parse_event_t::object_start)keys.emplace_back();
        else if(event==Json::parse_event_t::key)require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second);
        else if(event==Json::parse_event_t::object_end){require(!keys.empty());keys.pop_back();}
        return true;
    });}catch(const Json::exception &){throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
    require(command.is_object()&&command.contains("command")&&command["command"].is_string());
    auto name=command["command"].get<std::string>();require(name.size()<=32);
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    require(name=="get_tx_state"||name=="wspr_band_catalog"||name=="tone_start"||name=="tone_end"||
        name=="bounded_tone"||name=="stop"||name=="echo"||name=="shutdown"||name=="reboot");
    if(restore_maintenance_active()&&name!="get_tx_state"&&name!="wspr_band_catalog"&&name!="echo"&&name!="stop"&&name!="tone_end")
        throw LocalCommandRefused(LocalRefusal::Busy);
    return {{"schema","browser-reply/1"},{"message",socketServer.dispatch_local_command(message,session->tone_generation)}};
}
void backend_browser_disconnected(const std::string &connection) noexcept{
    try{
        std::shared_ptr<Session> session;auto &owner=sessions();
        {std::lock_guard lock(owner.mutex);const auto found=owner.values.find(connection);if(found==owner.values.end())return;
            session=std::move(found->second);owner.values.erase(found);}
        std::lock_guard lock(session->mutex);session->closed=true;
        socketServer.release_local_command_session(session->tone_generation);
    }catch(...){/* The tone lifecycle retains cleanup-failure evidence. */}
}
}
