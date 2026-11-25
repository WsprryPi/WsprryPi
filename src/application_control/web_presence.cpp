// SPDX-License-Identifier: MIT
#include "web_presence.hpp"
#include "config_handler.hpp"
#include "scheduling.hpp"
#include "web_server.hpp"
#include "application_storage/application_state.hpp"
#include <mutex>
namespace wsprrypi {
namespace {
struct Presence {std::mutex mutex;std::string connection;WebListeners listeners;nlohmann::json bootstrap;};
Presence &presence(){static Presence value;return value;}
void require(bool value){if(!value)throw LocalCommandRefused(LocalRefusal::InvalidRequest);}
std::optional<int> port(const nlohmann::json &value){
    require(value.is_null()||(value.is_number_integer()&&value>=1024&&value<=49151));
    return value.is_null()?std::nullopt:std::optional<int>(value.get<int>());
}
}
WebListeners web_service_listeners(){auto &slot=presence();std::lock_guard lock(slot.mutex);return slot.listeners;}
void prepare_backend_web_bootstrap(std::shared_ptr<SqliteStateStore> database,const std::string &boot){
    std::lock_guard lock(config_update_mutex());
    auto &slot=presence();std::lock_guard selected(slot.mutex);require(slot.bootstrap.is_null());
    slot.bootstrap={{"schema","web-service-bootstrap/1"},{"device_id",database->identity()},
        {"role",database->role()==StateRole::Pi?"pi":"controller"},{"boot",boot},
        {"web_port",config.web_port},{"socket_port",config.socket_port},
        {"http_enabled",web_server_start_enabled(config)},{"websocket_enabled",websocket_server_start_enabled(config)},
        {"http_loopback_only",webServer.loopback_fixture()},{"socket_loopback_only",config.socket_loopback_only},
        {"socket_loopback_family",config.socket_loopback_family==WebSocketLoopbackFamily::IPv4?"ipv4":config.socket_loopback_family==WebSocketLoopbackFamily::IPv6?"ipv6":"auto"}};
}
nlohmann::json web_service_bootstrap(std::shared_ptr<SqliteStateStore> database,const std::string &boot){
    auto &slot=presence();std::lock_guard selected(slot.mutex);
    require(!slot.bootstrap.is_null()&&slot.bootstrap["device_id"]==database->identity()&&slot.bootstrap["boot"]==boot);
    return slot.bootstrap;
}
nlohmann::json backend_web_presence(const LocalRequest &request){
    require(request.transport_connection.size()==32&&!exiting_wspr.load());auto &slot=presence();
    if(request.operation==LocalOperation::WebAttach){
        require(request.arguments.is_object()&&request.arguments.size()==2&&request.arguments.contains("web_port")&&request.arguments.contains("socket_port"));
        const WebListeners listeners{port(request.arguments["web_port"]),port(request.arguments["socket_port"])};
        {
            std::lock_guard selected(slot.mutex);
            require(!slot.bootstrap.is_null()&&listeners.http==(slot.bootstrap["http_enabled"]==true?std::optional<int>(slot.bootstrap["web_port"].get<int>()):std::nullopt)&&
                listeners.websocket==(slot.bootstrap["websocket_enabled"]==true?std::optional<int>(slot.bootstrap["socket_port"].get<int>()):std::nullopt));
            if(!slot.connection.empty())throw LocalCommandRefused(LocalRefusal::Busy);
            slot.connection=request.transport_connection;slot.listeners=listeners;
        }
    }else{
        require(request.operation==LocalOperation::WebHeartbeat&&request.arguments.empty());
        std::lock_guard selected(slot.mutex);require(slot.connection==request.transport_connection);
    }
    return {{"schema","web-service-presence/1"},{"connected",true}};
}
void backend_web_disconnected(const std::string &connection) noexcept{
    auto &slot=presence();
    {std::lock_guard lock(slot.mutex);
        if(slot.connection!=connection)return;
        slot.connection.clear();slot.listeners={};
    }
}
}
